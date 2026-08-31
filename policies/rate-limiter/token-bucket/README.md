# Token bucket

A key holds up to `burst` tokens. Each request spends one. The balance refills
at `ratePerSec` and stops at `burst`. That is the whole policy, and it gives you
the two properties most services actually want: a long-run ceiling of
`ratePerSec`, and the ability for a caller who has been quiet to spend what it
saved.

**This is the domain's recommended default.** Reach for something else only when
you know why.

Its advantage over the window policies is that it has no window. A
[fixed window](../fixed-window/) has a seam where twice the limit slips through.
A [sliding counter](../sliding-counter/) removes the seam but still treats a
caller's allowance as something tied to a window it aligns to. A token bucket
refills continuously, so a refused caller learns exactly how many milliseconds
to wait, and the answer is never "up to a whole window".

## When to use it

- **As the default for a public API.** Callers get a burst allowance for
  interactive use and a firm long-run rate, which is what almost every rate-limit
  policy document is trying to describe.
- **In front of anything with a queue or a pool.** The burst is bounded and
  known, `burst` exactly, so you can size the thing downstream against it.
- **When you want to be able to tell a caller when to come back.** `retryAfter`
  is exact and usually small. Returning it in a `Retry-After` header turns a
  refusal into something a client can act on rather than retry blindly.
- **When callers are bursty by nature.** A UI that fires six requests on page
  load is not abusive, and a limiter that refuses it is wrong. The burst
  allowance is how you say so.
- **With many keys.** Three integers per key, and no per-request allocation.

## When not to use it

- **When bursts are the thing you are protecting against.** If the downstream
  cost of `burst` requests arriving together is unacceptable, do not configure a
  burst, and once `burst` is 1 you are describing a
  [leaky bucket](../leaky-bucket/), which says so more clearly.
- **When you owe an exact "no more than N in any window" guarantee.** A token
  bucket admits up to `rate x window + burst` in a window, not `N`. If a
  contract uses the exact phrasing, [sliding log](../sliding-log/) is the policy
  that satisfies it literally.
- **When the limit must be enforced across processes without coordination.** The
  balance is a moving number, not a counter keyed by a window, so two
  independent instances each allow a full burst.
  [Fixed window](../fixed-window/) and [sliding counter](../sliding-counter/)
  shard far more gracefully.
- **When callers cannot handle being refused at all.** A limiter is not a queue.
  If the work must eventually happen, put it in a queue and rate-limit the
  drain, which is the shape a leaky bucket describes.

## How it works

Each key holds a token balance, a fractional carry, and the time it was last
touched.

```
elapsed = min(now - last, fillMs)        # fillMs = ceil(burst * 1000 / rate)
credit += rate * elapsed
tokens += credit div 1000
credit  = credit mod 1000
if tokens >= burst:
    tokens = burst
    credit = 0                           # the bucket overflows
last = now

if tokens < cost: return false
tokens -= cost
return true
```

**The ledger is integer arithmetic**. Tokens are
whole. The fraction lives in `credit`, measured in thousandths of a token. A
floating-point balance would drift differently in three languages and the drift
would eventually change a decision. At three tokens a second the boundary lands
on millisecond 334, not 333, and a vector pins it.

**Idle time is clamped to `fillMs` before the multiply.** A key untouched for a
month would otherwise compute `rate x elapsed` in the billions: harmless in
JavaScript, an overflow in C. The result is identical either way, because the
bucket saturates long before.

**A key never seen starts full.** It has been idle for all of history, and a
bucket that started empty would refuse a first request for a reason no caller
could act on.

**Tie-breaking.** A request is admitted when `tokens >= cost`, so one costing
exactly the balance passes. A cost above `burst` can never be met and is
refused rather than being allowed to wedge the key.

## Parameters

| Name | Type | Default | Description |
|---|---|---|---|
| `ratePerSec` | number | 100 | Tokens added per second. |
| `burst` | number | 100 | Maximum tokens a key can hold, and so the largest burst it can spend. |

The defaults are the registry's reference configuration: 100 permits per second,
burst 100.

## Complexity

O(1) per request: one hash lookup and a handful of integer operations. Space is
three integers per tracked key.

As with the window policies, keys are never forgotten. A deployment expires a
bucket after `fillMs` of inactivity, since a full bucket is indistinguishable
from a new one. Keeping them here puts the memory into the benchmark's
`entriesTracked` column rather than hiding it behind a sweep.

## Benchmark

<!-- bench:start -->
| Trace | Accept rate | Peak / 100 ms | Throughput |
|---|---:|---:|---:|
| `steady` | 1 | 22 | 42,500,000/s |
| `bursty` | 1 | 62 | 47,500,000/s |
| `many-keys` | 1 | 69 | 22,500,000/s |
| `overload` | 0.3429 (−0.3317) | 36 | 44,100,000/s |

Accept rate alone ranks nothing here: under sustained overload every correct limiter admits rate times time, so these converge by construction. Read the domain README, because the choice is made on memory, on behaviour at a window seam, and on what distributes. Throughput is machine-dependent and is never asserted.

<sub>Generated by `pnpm bench && pnpm render` from core 0.1.0. Do not edit.</sub>
<!-- bench:end -->

## Source

Folklore, and old: the token bucket is standard in traffic shaping and appears
in ATM and IP network literature from the 1980s onward. It has no single
citable origin, which is itself worth knowing: it is a shape, not a paper.

**Its nearest neighbour is [leaky bucket](../leaky-bucket/), and they are the
same algorithm.** Substituting `tokens = capacity - level` turns each line of
one into the corresponding line of the other. A vector in each pins the
equivalence and `bucket-policies.test.ts` checks it across 20,000 randomised
decisions. The choice between them is about which parameter you find natural to
state, not about behaviour. [GCRA](../gcra/) is a third spelling of the same
idea, holding one timestamp instead of a balance.

## Notes

No patents known.

Keys are integers: callers with string keys hash them, and `mix32` is the
registry's default.

The C port takes an extra `max_keys` parameter and refuses keys beyond it, since
it allocates everything up front and never grows. That is a property of the C
API rather than of the policy, and it is documented in `token_bucket.h`.
