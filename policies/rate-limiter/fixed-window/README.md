# Fixed window

Divide time into windows of `windowMs`, keep one counter per key, and refuse
anything past `limit` until the window rolls over. Two integers per key, no
allocation on the hot path, and a counter that a Redis `INCR` with a TTL
implements exactly, which is why it is the limiter most services start with,
and often the one they keep.

It makes one bet: that you care about the *average* rate over a window, not
about what happens at the seam between two of them. When that bet is wrong it
is wrong by a factor of two, and the failure is easy to trigger by accident.

## When to use it

- **A quota, not a throughput limit.** "10,000 API calls per month", "100
  uploads per day". The window is long, the seam is a rounding error, and the
  semantics match what you actually promised the customer.
- **You are limiting in more than one process** and do not want them talking to
  each other. Windows here are aligned to the epoch, so two servers agree on
  which window it is with no coordination at all: just `INCR key:<window>` with
  a TTL. Neither [sliding log](../sliding-log/) nor a token bucket is anywhere
  near as easy to distribute.
- **Memory is the binding constraint.** Two integers per key is the floor among
  the window policies. The sliding log wants a hundred times that.
- **The downstream service has real headroom.** If it can absorb `2 x limit` for
  a moment without harm, the boundary burst is a fact about the metric rather
  than an incident.

## When not to use it

- **Anything that can actually be overwhelmed by `2 x limit`.** A connection
  pool, a rate-limited upstream API you are proxying, a thread pool: the burst
  is not theoretical, and a client with a retry loop will find it on its own.
  Reach for [sliding counter](../sliding-counter/) instead: it removes the burst
  for one more integer per key.
- **Short windows.** The shorter the window, the more often the seam comes
  around and the larger the burst is relative to the traffic. At a one-second
  window the flaw is reachable every single second.
- **When you owe someone an exact guarantee.** "No more than 100 in any 60
  seconds" is not what this enforces, and if that sentence appears in a
  contract, use [sliding log](../sliding-log/).
- **Adversarial clients.** The burst is trivially exploitable once noticed: wait
  for the window edge, then send twice your budget. Nothing about the policy
  resists a client that has read this page.

## How it works

Each key holds the start of the window its count belongs to, and the count.

```
window = now - (now mod windowMs)        # integer, aligned to the epoch
if bucket.window != window:
    bucket.window = window
    bucket.count  = 0                    # the discontinuity
if bucket.count + cost > limit:
    return false
bucket.count += cost
return true
```

**Windows are aligned to the epoch**, not to a key's first request. This is the
choice that makes the design distributable: the window index is a pure function
of the clock, so no process has to remember when a key first appeared. It also
means a key's first request does not get a fresh window to itself. A request at
990 ms with a 100 ms window has ten milliseconds before its budget resets, not
a hundred.

**`retryAfter` is exact.** The counter resets at the window edge and nothing
before then can change the answer, so the wait is simply the remainder of the
window. That is unusual: the other two policies in this family have to reason
about decay, and one of them can only offer a hint.

**Tie-breaking.** A request is admitted when `count + cost <= limit`, so one
that costs exactly the remaining budget passes. A request costing more than
`limit` can never be admitted at all, even against an empty counter: it is
refused rather than being allowed to wedge the key permanently.

## Parameters

| Name | Type | Default | Description |
|---|---|---|---|
| `limit` | number | 100 | Requests allowed per window. |
| `windowMs` | number | 1000 | Window length, in milliseconds. |

The defaults are the registry's reference configuration: 100 permits per
second.

## Complexity

O(1) per request: one hash lookup, one comparison, one increment. Space is two
integers per tracked key.

Keys are never forgotten here. A production deployment gives each counter a TTL
of one window, which is exactly what the Redis idiom does. This implementation
keeps the state so that the memory cost shows up in the benchmark's
`entriesTracked` column instead of being hidden behind a background sweep.

## Benchmark

<!-- bench:start -->
| Trace | Accept rate | Peak / 100 ms | Throughput |
|---|---:|---:|---:|
| `steady` | 0.9927 (−0.0073) | 22 | 72,800,000/s |
| `bursty` | 0.9692 (−0.0308) | 62 | 92,600,000/s |
| `many-keys` | 1 | 69 | 29,700,000/s |
| `overload` | 0.3374 (−0.3373) | 46 | 89,700,000/s |

Accept rate alone ranks nothing here: under sustained overload every correct limiter admits rate times time, so these converge by construction. Read the domain README, because the choice is made on memory, on behaviour at a window seam, and on what distributes. Throughput is machine-dependent and is never asserted.

<sub>Generated by `pnpm bench && pnpm render` from core 0.1.0. Do not edit.</sub>
<!-- bench:end -->

## Source

Folklore. The fixed-window counter predates anyone's attempt to write it down.
It is what you get by asking "how many this minute?" and it appears in every
API gateway, usually as the default.

Its nearest neighbour is [sliding counter](../sliding-counter/), which keeps the
same two counters plus one more and weights them: the boundary burst
disappears and the state stays O(1) per key. If you can afford three integers
instead of two, there is very little reason to choose this over that, except
that this one is simpler to explain and to implement in a stored procedure.

## Notes

No patents known. The Redis idiom (`INCR` on a key named for the window, with
`EXPIRE`) is the canonical distributed implementation and matches this policy
exactly, including the boundary behaviour.

Keys are integers: callers with string keys hash them, and `mix32` is the
registry's default.
