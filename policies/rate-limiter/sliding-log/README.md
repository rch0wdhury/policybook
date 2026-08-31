# Sliding log

Keep the timestamp of every admitted request, drop the ones that have aged out,
and admit if fewer than `limit` remain. It is the only limiter in this domain
that enforces its limit *exactly*: over any window of `windowMs` ending at any
instant, the number of admitted requests is at most `limit`. No boundary effect,
no estimate, no burst that slips through.

That exactness is bought with memory, and the price is the whole story. The ring
holds `limit` timestamps **per key**. A 100/s limit over a million keys is a
hundred million timestamps, about 800 MB, where
[fixed window](../fixed-window/) would use two integers per key and
[GCRA](../gcra/) one.

## When to use it

- **The limit is a promise you have to keep.** A contractual quota, a regulated
  interface, an upstream that will ban you for exceeding a documented rate.
  This is the only policy here that makes the guarantee literally true.
- **Requests are expensive relative to a timestamp.** If each admitted request
  costs a model inference or a payment authorisation, eight bytes of bookkeeping
  per permit is not the number you should be optimising.
- **Few keys, or a small limit.** Ten thousand keys at a limit of 20 is 200,000
  timestamps, entirely unremarkable. The memory problem is a product of both
  factors, and it only bites when both are large.
- **You need to explain a refusal.** The log holds exactly which requests are
  counted against the caller, so `retryAfter` is exact and the decision is
  auditable in a way an estimate never is.

## When not to use it

- **Many keys with a large limit.** This is the failure mode, and it is a memory
  blow-up rather than a slowdown: `limit x keys` timestamps, all resident.
  [Sliding counter](../sliding-counter/) gets you almost the same behaviour for
  three integers per key.
- **As a general-purpose default.** The exactness is rarely worth its cost. Most
  services want "roughly this rate, no nasty bursts", which the sliding counter
  delivers at a hundredth of the memory.
- **Distributed limiting.** Sharing a log across processes means shipping
  timestamps around. Sharing a counter means one integer. The other policies
  here distribute far more gracefully.
- **When the limit can be reconfigured at runtime.** The ring is sized to
  `limit` at construction, so changing it means rebuilding the state rather than
  writing a new number.

## How it works

Each key holds a ring buffer of `limit` timestamps, oldest at the head.

```
cutoff = now - windowMs
while count > 0 and times[head] <= cutoff:   # amortised O(1)
    head = (head + 1) mod limit
    count -= 1

if count + cost > limit:
    return false
repeat cost times:
    times[(head + count) mod limit] = now
    count += 1
return true
```

The ring is sized to `limit` because that is the most that can ever be live at
once: the moment a `limit + 1`-th entry would be needed, the request that
wanted it was refused.

**Expiry is O(1) amortised**, not O(limit) per call. Each timestamp is written
once and dropped once, so the `while` loop does constant work per admitted
request however bursty the arrivals are.

**The window is `(now - windowMs, now]`**, half-open at the far end. An entry
exactly `windowMs` old has left. That is the choice that makes "at most `limit`
in any `windowMs` interval" true as stated rather than off by one at the edge,
and it is pinned by a vector.

**Tie-breaking.** A request costing `n` occupies `n` slots, all stamped with the
same instant, so they age out together. A request costing more than `limit` is
refused rather than being allowed to wedge the key.

## Parameters

| Name | Type | Default | Description |
|---|---|---|---|
| `limit` | number | 100 | Requests allowed in any window of `windowMs`. |
| `windowMs` | number | 1000 | Window length, in milliseconds. |

The defaults are the registry's reference configuration: 100 permits per
second and, not incidentally, 100 timestamps per key.

## Complexity

O(1) amortised per request. Space is O(`limit`) per tracked key: the ring is
allocated at its full size when a key is first seen, so the cost is
`limit x keys` timestamps whether or not the keys are busy.

Timestamps are held as doubles rather than 32-bit integers on purpose: a
millisecond clock passes 2^32 after 49 days, and a limiter should not develop a
fault on day 50. A double holds every integer below 2^53 exactly, so this is
still integer arithmetic.

## Benchmark

<!-- bench:start -->
| Trace | Accept rate | Peak / 100 ms | Throughput |
|---|---:|---:|---:|
| `steady` | 0.9772 (−0.0228) | 22 | 32,700,000/s |
| `bursty` | 0.9692 (−0.0308) | 62 | 46,700,000/s |
| `many-keys` | 1 | 69 | 11,200,000/s |
| `overload` | 0.3374 (−0.3373) | 38 | 49,700,000/s |

Accept rate alone ranks nothing here: under sustained overload every correct limiter admits rate times time, so these converge by construction. Read the domain README, because the choice is made on memory, on behaviour at a window seam, and on what distributes. Throughput is machine-dependent and is never asserted.

<sub>Generated by `pnpm bench && pnpm render` from core 0.1.0. Do not edit.</sub>
<!-- bench:end -->

## Source

Folklore. The sliding window log is the obvious correct implementation: it is
what you write if you take the definition of a rate limit literally. Its cost
is why everything else in this domain exists.

Its nearest neighbour is [sliding counter](../sliding-counter/), which
approximates this behaviour with two counters. The comparison is the clearest in
the domain: after a burst at a window edge, this policy holds every request
until they all expire together, while the counter fades the budget back in
continuously. Neither is more correct: the log is exact about a guarantee, the
counter is smoother about recovery.

## Notes

No patents known.

Keys are integers: callers with string keys hash them, and `mix32` is the
registry's default.

The name is used loosely in the wild. "Sliding window" sometimes means this and
sometimes means the two-counter approximation, which is why the registry spells
the two out as `sliding-log` and `sliding-counter` rather than making a reader
guess which one a page is about.
