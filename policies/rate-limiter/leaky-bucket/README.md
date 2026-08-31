# Leaky bucket

Every admitted request adds one unit to a bucket that leaks continuously at
`ratePerSec`. A request that would overflow `capacity` is refused. At the
default capacity of 1 that means exact spacing: one request every
`1000 / ratePerSec` milliseconds, never two together. That is the smoothing
the policy is named for.

**At equal parameters this is the [token bucket](../token-bucket/), exactly.**
Substituting `tokens = capacity - level` turns every line of one into the
corresponding line of the other: a request that fits under the ceiling is a
request with a token to spend, draining is refilling, and the level bottoming
out at zero is the balance saturating at `burst`. Neither approximates the
other. A vector in each policy pins the equivalence and
`bucket-policies.test.ts` checks it across 20,000 randomised decisions.

So the choice between them is about vocabulary, and about which parameter you
find it natural to state. That is not a small thing, since a limiter you
configure wrongly is worse than one you understand, but it is worth being clear
that no behaviour hangs on it.

## When to use it

- **When smoothing is the goal, not budgeting.** If the sentence you want is
  "no more than one of these at a time, paced evenly", this says it in one
  parameter. The token bucket can express the same thing with `burst: 1`, but
  nobody reading that configuration will guess it was deliberate.
- **In front of something that hates concurrency more than volume.** A
  single-threaded downstream, a device with one serial port, a legacy system
  that serialises anyway: the value is the spacing, not the count.
- **When your team already thinks in queue depth.** "How much may back up before
  we shed load?" is the natural question here, and `capacity` answers it
  directly.
- **As the metering half of a shaper.** If you are queueing requests and
  draining them at a fixed rate, this is the admission test for the queue.

## When not to use it

- **When callers legitimately burst.** At its default capacity this refuses the
  second of two requests arriving together, which for an interactive client is
  usually wrong. Raise `capacity` or, more honestly, use the
  [token bucket](../token-bucket/), whose name says a burst is expected.
- **When you would only ever set `capacity` equal to a burst allowance.** You
  are describing a token bucket, and the token bucket's parameter names will
  read correctly to whoever maintains the configuration.
- **When you need an exact "N in any window" guarantee.** Like the token bucket
  it admits up to `rate x window + capacity` in a window.
  [Sliding log](../sliding-log/) is the policy that satisfies that phrasing
  literally.
- **Across processes without coordination.** The level is a moving number rather
  than a counter keyed by a window, so independent instances each allow a full
  capacity. The window policies shard better.

## How it works

Each key holds a level, a fractional carry, and the time it was last touched.

```
elapsed = min(now - last, drainMs)       # drainMs = ceil(capacity * 1000 / rate)
credit += rate * elapsed
drained = credit div 1000
credit  = credit mod 1000
if drained >= level:
    level  = 0
    credit = 0                           # the bucket has run dry
else:
    level -= drained
last = now

if level + cost > capacity: return false
level += cost
return true
```

**The ledger is integer arithmetic**. The level
is whole. The fraction of a unit lives in `credit`, measured in thousandths. At
three units a second the boundary lands on millisecond 334, not 333, and a
vector pins it: the same millisecond the token bucket crosses, which is one of
the ways the equivalence is checked.

**Idle time is clamped to `drainMs` before the multiply**, so a key untouched
for a month cannot overflow the arithmetic in the C port. The result is
identical either way, because the bucket empties long before.

**A key never seen starts empty.** It has been draining for all of history.

**Tie-breaking.** A request is admitted when `level + cost <= capacity`, so one
filling exactly the remaining room passes. A cost above `capacity` can never
fit and is refused rather than being allowed to wedge the key.

## Parameters

| Name | Type | Default | Description |
|---|---|---|---|
| `ratePerSec` | number | 100 | Units drained per second. |
| `capacity` | number | 1 | Maximum level, and so the largest burst that fits at once. |

**The default capacity is 1, not the domain's reference burst of 100.** The
reference configuration maps onto `capacity` directly, and setting it to 100
would make this policy indistinguishable from the token bucket, which is
exactly the point, but it is not what someone reaching for a *leaky* bucket
wants. A caller who wants a burst allowance is describing a token bucket.

## Complexity

O(1) per request: one hash lookup and a handful of integer operations. Space is
three integers per tracked key.

## Benchmark

<!-- bench:start -->
| Trace | Accept rate | Peak / 100 ms | Throughput |
|---|---:|---:|---:|
| `steady` | 1 | 22 | 49,100,000/s |
| `bursty` | 1 | 62 | 59,000,000/s |
| `many-keys` | 1 | 69 | 26,400,000/s |
| `overload` | 0.3429 (−0.3317) | 36 | 48,700,000/s |

Accept rate alone ranks nothing here: under sustained overload every correct limiter admits rate times time, so these converge by construction. Read the domain README, because the choice is made on memory, on behaviour at a window seam, and on what distributes. Throughput is machine-dependent and is never asserted.

<sub>Generated by `pnpm bench && pnpm render` from core 0.1.0. Do not edit.</sub>
<!-- bench:end -->

## Source

Folklore, and older than most of what it is compared against: the leaky bucket
appears in telecoms traffic-shaping literature from the 1980s, where the image
of a bucket with a hole in it did real explanatory work.

**Its nearest neighbour is [token bucket](../token-bucket/), which is the same
algorithm**: see the equivalence discussion above. [GCRA](../gcra/) is a third
spelling, holding one timestamp rather than a level. The registry ships all
three because the names are what people search for, and knowing that two of them
are one algorithm is more useful than being shown only one.

## Notes

No patents known.

**The name is genuinely ambiguous in the wild**, which is worth stating plainly.
"Leaky bucket" is used for two different things: this *meter*, which counts
against a leaking level and refuses overflow, and a *shaper*, which queues
requests and releases them at a fixed rate so nothing is ever refused. This
policy is the meter: a limiter cannot hold requests. If you want the shaper,
you want a queue, and this is the admission test you would put in front of it.

Keys are integers: callers with string keys hash them, and `mix32` is the
registry's default.

The C port takes an extra `max_keys` parameter and refuses keys beyond it, since
it allocates everything up front and never grows. That is a property of the C
API rather than of the policy, and it is documented in `leaky_bucket.h`.
