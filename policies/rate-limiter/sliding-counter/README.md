# Sliding counter

Keep the count for the current window and the previous one, then estimate the
rate over the trailing window by fading the old count out as the new one fills:

```
estimate = previous * (windowMs - elapsed) / windowMs + current
```

Three integers per key instead of a hundred timestamps, and the boundary burst
is gone: a client that spent its whole budget just before the edge finds that
budget still counted against it just after, decaying smoothly to nothing over
the following window.

This is the compromise most production limiters actually run, and for good
reason: it costs one integer more than [fixed window](../fixed-window/) and
removes that policy's only serious flaw.

## When to use it

- **As the default window limiter.** If you were about to reach for a fixed
  window, reach for this instead unless you specifically need the fixed
  window's stored-procedure simplicity. Same order of memory, no boundary
  burst.
- **Short windows and real throughput limits.** Per-second limiting in front of
  a connection pool or an upstream API is exactly where the fixed window's seam
  hurts and where this policy's smooth recovery is worth having.
- **Many keys.** Three integers each scales to millions of keys. A
  [sliding log](../sliding-log/) at the same limit would not.
- **Distributed limiting.** Two counters per key with epoch-aligned windows
  distribute nearly as easily as the fixed window's one, with two `INCR`s and a
  little arithmetic on read, and far more easily than a log of timestamps.

## When not to use it

- **When you owe an exact guarantee.** This is an estimate and it can be wrong
  in both directions. If a contract or a regulator says "no more than 100 in any
  60 seconds", use [sliding log](../sliding-log/), which makes that literally
  true.
- **Extremely bursty traffic where the shape matters.** The formula assumes the
  previous window's requests were spread evenly across it. When they were all at
  the very end, the estimate reads low and slightly more than `limit` gets
  through. When they were all at the start, it reads high and refuses requests
  it could have allowed.
- **When you want to *reward* saving up.** This is a rate limiter, not a budget:
  a caller silent for a window gets its allowance back and no more. If a client
  should be able to bank an idle period and spend it in one burst, that is a
  [token bucket](../token-bucket/).
- **Very short windows relative to clock granularity.** With a window of a few
  milliseconds the linear fade is quantised so coarsely that the estimate stops
  meaning much, and a token bucket's continuous ledger is a better fit.

## How it works

Each key holds the aligned start of the current window and two counts.

```
window = now - (now mod windowMs)
if window != counter.window:
    previous = (window == counter.window + windowMs) ? current : 0
    current  = 0
    counter.window = window

elapsed  = now - window
carried  = previous * (windowMs - elapsed) div windowMs      # floored
if carried + current + cost > limit:
    return false
current += cost
return true
```

A gap of two windows or more clears **both** counts: nothing from before is
still inside the trailing window, so shifting the old count forward would count
traffic that has entirely left.

**The weighting is integer arithmetic**, with the remainder discarded. This is not a rounding detail. With `limit = 5`
one millisecond into a new window, the exact carried value is
`5 x 999/1000 = 4.995`. Flooring makes it 4, which admits a request an exact
calculation would refuse. Flooring makes the estimate err very slightly low,
and, more importantly, it makes the TypeScript, Python and C ports reach
identical decisions rather than diverging on the last bit of a double. A vector
pins the case where the floor changes the answer.

**`retryAfter` is solved, not searched.** Admission needs the carried part to
fall to `limit - current - 1` or below, and since it decays linearly the
smallest qualifying `elapsed` follows directly:

```
carried <= need  <=>  previous * (windowMs - elapsed) < (need + 1) * windowMs
                 <=>  elapsed > windowMs * (previous - need - 1) / previous
```

When the current window has already reached the limit on its own, the wait runs
to the window edge **plus one millisecond**: at the edge this count becomes the
previous count and, undecayed, still refuses. That extra millisecond is the
difference between a usable hint and a useless one, and a property test in
`window-policies.test.ts` checks that waiting exactly `retryAfter` always
admits.

**Tie-breaking.** A request is admitted when `estimate + cost <= limit`, so one
costing exactly the remaining budget passes.

## Parameters

| Name | Type | Default | Description |
|---|---|---|---|
| `limit` | number | 100 | Requests allowed per window. |
| `windowMs` | number | 1000 | Window length, in milliseconds. |

The defaults are the registry's reference configuration: 100 permits per
second.

## Complexity

O(1) per request: one hash lookup, one multiply, one integer division. Space is
three integers per tracked key.

As with the fixed window, keys are never forgotten. A deployment gives each
counter a TTL of two windows. Keeping them here puts the memory cost into the
benchmark's `entriesTracked` column rather than hiding it behind a sweep.

## Benchmark

<!-- bench:start -->
| Trace | Accept rate | Peak / 100 ms | Throughput |
|---|---:|---:|---:|
| `steady` | 0.9847 (−0.0153) | 22 | 21,000,000/s |
| `bursty` | 0.9692 (−0.0308) | 62 | 22,600,000/s |
| `many-keys` | 1 | 69 | 12,400,000/s |
| `overload` | 0.3373 (−0.3374) | 36 | 22,100,000/s |

Accept rate alone ranks nothing here: under sustained overload every correct limiter admits rate times time, so these converge by construction. Read the domain README, because the choice is made on memory, on behaviour at a window seam, and on what distributes. Throughput is machine-dependent and is never asserted.

<sub>Generated by `pnpm bench && pnpm render` from core 0.1.0. Do not edit.</sub>
<!-- bench:end -->

## Source

Folklore, though Cloudflare's 2017 write-up of running it on production traffic
is the reference most people mean when they cite it. Their measurement, that
the approximation misclassifies well under one percent of requests on real
workloads, is the empirical case for choosing it over an exact log.

Its two neighbours bracket it exactly. [Fixed window](../fixed-window/) is this
policy without the `previous` term, and it is that missing term that lets
`2 x limit` through a boundary. [Sliding log](../sliding-log/) is this policy
with perfect information instead of an estimate, at a hundred times the memory.

## Notes

No patents known.

Keys are integers: callers with string keys hash them, and `mix32` is the
registry's default.

The name "sliding window" is used for both this and the log in the wild, which
is why the registry spells both out rather than making a reader guess.
