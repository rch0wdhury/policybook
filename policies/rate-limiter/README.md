# rate-limiter

Policies for deciding whether a request may proceed. They differ far less
in *how much* they let through than most comparisons suggest, and far more in
what they cost to run and how they behave at the edges.

**Start with [token bucket](token-bucket/).** It is the default for good
reasons: continuous refill with no window seam, an exact `retryAfter`, three
integers per key, and a burst allowance you state directly. Choose something
else when one of the rows below applies to you.

## Choosing one

| If you need | Use | Because |
|---|---|---|
| A sensible default | **[token bucket](token-bucket/)** | Bursts allowed and bounded, no seam, exact retry hint. |
| The same thing with the least state | **[GCRA](gcra/)** | Identical decisions from **one integer** per key instead of three: 34 bytes against 42. |
| Smoothing, not budgeting | **[leaky bucket](leaky-bucket/)** | Its default capacity of 1 forces even spacing. At equal parameters it *is* the token bucket. |
| An exact "N in any window" guarantee | **[sliding log](sliding-log/)** | The only policy that makes that sentence literally true, at 834 bytes per key. |
| To limit across processes without coordination | **[sliding counter](sliding-counter/)** | Epoch-aligned windows shard with two counters and no messages. |
| The simplest thing a stored procedure can do | **[fixed window](fixed-window/)** | One `INCR` with a TTL, and a boundary where twice the limit slips through. |
| RPM *and* TPM, the LLM-API shape | **[dual bucket](dual-bucket/)** | Two ceilings checked together, charged atomically. |

## The comparison nobody makes, and should

**Under sustained overload, every correct limiter admits almost exactly the same
traffic.** That is not a weakness of the benchmark. It is arithmetic. A limiter
configured for 100 permits a second admits about `rate x time`, whoever wrote
it. On the `overload` trace, 300 requests a second against a 100/s budget for a
minute, six of the seven policies land between 0.3373 and 0.3429.

So accept rate cannot choose for you, and a comparison built around it is
theatre. What actually differs:

### Memory, which differs by 24x

Measured from the C implementations at the reference budget over 1,024 keys
(`packages/c/tests/measure_ratelimiter_memory.c`), not estimated:

| Policy | Bytes per key |
|---|---:|
| [GCRA](gcra/) | **34.1** |
| [fixed window](fixed-window/) | 38.1 |
| [sliding counter](sliding-counter/) | 42.1 |
| [token bucket](token-bucket/) | 42.1 |
| [leaky bucket](leaky-bucket/) | 42.1 |
| [dual bucket](dual-bucket/) | 50.1 |
| [sliding log](sliding-log/) | **834.1** |

At a million keys that is 34 MB against 834 MB. The sliding log's exactness is
real and so is its price. Everything else is within a factor of one and a half,
and GCRA's edge is one integer where the others keep three.

### Behaviour at a window seam

Only the window policies have a seam, and only one of them mishandles it. With a
five-permit limit, five requests at 999 ms and five more at 1,000 ms:

| Policy | Admitted |
|---|---:|
| [fixed window](fixed-window/) | **10**: twice the limit in two milliseconds |
| [sliding counter](sliding-counter/) | 5 |
| [sliding log](sliding-log/) | 5 |

The bucket policies have no window and so no seam. This is pinned by
`window-policies.test.ts` rather than asserted here.

### What can be distributed

Epoch-aligned counters ([fixed window](fixed-window/),
[sliding counter](sliding-counter/)) shard across processes with no
coordination: the window index is a pure function of the clock. A moving balance
([token bucket](token-bucket/), [leaky bucket](leaky-bucket/),
[GCRA](gcra/)) does not: two independent instances each allow a full burst,
though GCRA's single integer is small enough to compare-and-swap in a shared
store. A [sliding log](sliding-log/) means shipping timestamps around.

## Three names, one algorithm

[token bucket](token-bucket/), [leaky bucket](leaky-bucket/) and [GCRA](gcra/)
are the same rule written three ways, and the registry says so with proof rather
than assertion. Substituting `tokens = capacity - level` turns the first into
the second. GCRA folds the balance and its fractional carry into a single
scheduled instant. `bucket-policies.test.ts` checks all three agree across
20,000 randomised decisions: every admission, every `retryAfter`, and the
visible state. `test_ratelimiter.c` re-checks it in C.

Knowing that is more useful than being shown only one of them: these are the
three names people search for, and the choice between them is state size and
vocabulary, not behaviour.

## Traces

| Trace | Shape | What it is for |
|---|---|---|
| `steady` | 5,491 arrivals, ~90/s on one key | Just *under* the limit. Shows which policies leak capacity anyway. |
| `bursty` | 3,020 arrivals, thirty 200 ms bursts at 500/s | Whether a policy can save up during silence. |
| `many-keys` | 59,877 arrivals over 7,278 keys | Per-key bookkeeping cost. |
| `overload` | 18,000 arrivals, 300/s on one key | Three times the limit, sustained. Demonstrates the convergence above. |

Full specifications in
[`TRACES.md`](../../packages/core/src/domains/rate-limiter/TRACES.md).

## Benchmark

<!-- bench:start -->
Accept rate on each canonical trace, best first on `overload`.

| Policy | `steady` | `bursty` | `many-keys` | `overload` |
|---|---:|---:|---:|---:|
| [Dual bucket](dual-bucket/) | 1 | 1 | 1 | 0.6747 |
| [GCRA](gcra/) | 1 | 1 | 1 | 0.3429 |
| [Leaky bucket](leaky-bucket/) | 1 | 1 | 1 | 0.3429 |
| [Token bucket](token-bucket/) | 1 | 1 | 1 | 0.3429 |
| [Fixed window](fixed-window/) | 0.9927 | 0.9692 | 1 | 0.3374 |
| [Sliding log](sliding-log/) | 0.9772 | 0.9692 | 1 | 0.3374 |
| [Sliding counter](sliding-counter/) | 0.9847 | 0.9692 | 1 | 0.3373 |

<sub>Generated by `pnpm bench && pnpm render` from core 0.1.0. Do not edit.</sub>
<!-- bench:end -->

### Reading the table

**`steady` separates the leakers.** At 90 requests a second against a 100/s
limit nothing *should* be refused, and the bucket policies refuse nothing. The
window policies refuse between 0.7% and 2.3%, because an unlucky cluster inside
one window is over the limit even though the average is not. Small, and real.

**`bursty` separates saving-up from smoothing.** A bucket with a burst
allowance absorbs each 100-request burst whole (1.0000). The window policies
refuse about 3% because a burst can straddle a window edge.

**`many-keys` shows nothing, deliberately.** At about eight arrivals per key
over two minutes, no per-key limit can bite. The trace is there for
`entriesTracked` and for the memory table above.

**`overload` shows the convergence** and one outlier:
[dual bucket](dual-bucket/) admits 0.6747 where everything else admits ~0.34.
That is not a misconfiguration: its burst allowance *is* its whole per-minute
quota, by construction, which is exactly how RPM-style limits behave in the
wild. A caller granted 6,000 requests a minute may spend all 6,000 in the first
second. Anyone deploying per-minute limits should know that, and the benchmark
is where it becomes visible.

## The interface

```ts
allow(key: number, cost: number, now: number): boolean
retryAfter?(key: number, now: number): number
stateSize?(): number
```

Time is an integer number of milliseconds supplied by the caller: a policy
never reads a clock. Every arithmetic operation on it is integer arithmetic too:
milli-unit ledgers with an explicit carry rather than floating-point balances,
because floats drift and three languages drift differently. Keys are integers:
callers with string keys hash their own, and `mix32` is the registry's default.

See
[`interface.ts`](../../packages/core/src/domains/rate-limiter/interface.ts) for
the full contract.
