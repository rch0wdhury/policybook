# GCRA

The Generic Cell Rate Algorithm stores one number per key: the **theoretical
arrival time**, the instant at which the next request would be exactly on
schedule. How many permits are banked and how much of the next one has accrued
are both implied by how far that instant sits from now.

It admits and refuses **exactly** what [token bucket](../token-bucket/) does,
including the fractional carry, with `retryAfter` agreeing to the millisecond.
The TAT is not an approximation of a balance. It is the same information written
differently, and a cross-policy test checks that over 20,000 randomised
decisions rather than taking it on faith.

So the reason to choose it is **state**: one integer per key rather than three.
At a million keys that is 8 MB against 24 MB, which is why GCRA is what you find
inside Redis rate-limiter modules and network equipment.

## When to use it

- **At scale, where per-key state is the cost that matters.** A million tracked
  keys is where the difference between one integer and three stops being
  academic.
- **In a shared store.** The whole state is a single integer, so a distributed
  implementation is one `GET`, a comparison, and one `SET`, small enough to do
  atomically in a Lua script or a compare-and-swap, which a three-field balance
  is not.
- **In constrained environments.** Firmware, packet processing, an eBPF map: the
  algorithm exists because ATM hardware could not afford anything larger.
- **When you already want a token bucket.** The behaviour is identical, so
  choosing this costs nothing but the explanation.

## When not to use it

- **When the code will be read more often than it runs.** "Tokens in a bucket"
  is a picture everyone shares, but "theoretical arrival time" needs a
  paragraph. If the limiter lives somewhere a hurried reader must modify it
  correctly, the [token bucket](../token-bucket/) spells its state out.
- **When you want to inspect or expose the balance.** The permit count here is
  derived, not stored, so a dashboard showing "requests remaining" has to
  reconstruct it. That is one subtraction, but it is one more thing to get right.
- **When you need an exact "N in any window" guarantee.** Like the token bucket
  it admits up to `rate x window + burst` in a window.
  [Sliding log](../sliding-log/) is the policy that satisfies that literally.
- **When more than one limit applies.** Two dimensions means two TATs and no
  saving: [dual bucket](../dual-bucket/) is the shape for that.

## How it works

Each key holds a TAT, in units scaled by the rate.

```
scaled = now * ratePerSec                      # one ms is `ratePerSec` units
tolerance = (burst - 1) * 1000                 # one permit is 1000 units

if cost > burst: return false
if scaled < tat - tolerance + (cost - 1) * 1000: return false
tat = max(scaled, tat) + cost * 1000
return true
```

**The arithmetic is exact integers, scaled by the rate.** Written the textbook
way, GCRA needs an emission interval `T = 1000 / ratePerSec` milliseconds, which
is not a whole number for most rates: 3 permits a second gives 333.33.
Multiplying through by `ratePerSec` clears it: one permit costs exactly 1,000
scaled units and one millisecond is `ratePerSec` of them, so nothing is ever
rounded. That is also precisely why this agrees with the token bucket's
thousandths-of-a-token carry rather than merely resembling it: they are the same
rational number, held in different variables. A vector pins the case: at three
permits a second the boundary lands on millisecond 334, in both policies.

**`max(scaled, tat)` is what caps the burst.** Without it a key idle for a day
would have a TAT a day in the past and could spend a day's worth of permits at
once. The `max` restarts the schedule from now.

**The explicit `cost > burst` check is necessary.** The conformance test alone
does not cap: for a key whose TAT sits far enough in the past, any cost passes
it. The ceiling has to be stated.

**Exactness bound.** The TAT stays exact while `now * ratePerSec` is below 2^53,
which at 100 permits a second is about 2,800 years of millisecond clock. The C
port uses 64-bit integers and has no such bound worth mentioning.

**Tie-breaking.** Identical to the token bucket's, necessarily: a request
costing exactly the available permits is admitted.

## Parameters

| Name | Type | Default | Description |
|---|---|---|---|
| `ratePerSec` | number | 100 | Permits per second, sustained. |
| `burst` | number | 100 | How many permits may be spent at once after an idle period. |

`burst` maps onto the classical **τ** (burst tolerance) as `(burst - 1) x T`.
Stating it as a permit count rather than a duration keeps it comparable with
every other policy in this domain. At `burst` 1 the tolerance is zero and the
policy enforces exact spacing, which is GCRA at its most literal.

## Complexity

O(1) per request: one hash lookup, one comparison, one addition. Space is **one
integer per tracked key**, which is the whole point.

## Benchmark

<!-- bench:start -->
| Trace | Accept rate | Peak / 100 ms | Throughput |
|---|---:|---:|---:|
| `steady` | 1 | 22 | 55,700,000/s |
| `bursty` | 1 | 62 | 65,700,000/s |
| `many-keys` | 1 | 69 | 31,000,000/s |
| `overload` | 0.3429 (−0.3317) | 36 | 73,000,000/s |

Accept rate alone ranks nothing here: under sustained overload every correct limiter admits rate times time, so these converge by construction. Read the domain README, because the choice is made on memory, on behaviour at a window seam, and on what distributes. Throughput is machine-dependent and is never asserted.

<sub>Generated by `pnpm bench && pnpm render` from core 0.1.0. Do not edit.</sub>
<!-- bench:end -->

## Source

The ATM Forum's Traffic Management Specification 4.0 (1996) defines the GCRA as
the conformance test for cell arrivals, in the "virtual scheduling" and
"continuous-state leaky bucket" forms, which that document itself notes are
equivalent. This implementation is the virtual-scheduling form, which is the one
that stores a single timestamp.

**Its neighbours are [token bucket](../token-bucket/) and
[leaky bucket](../leaky-bucket/), and all three are one algorithm.** The registry
ships all of them because these are the three names people search for, and
knowing they are the same idea is more useful than being shown only one. The
differences that survive are state size (one integer here, three there) and how
readable the code is to someone who has not met the algorithm before.

## Notes

No patents known. The specification is a published industry standard.

Keys are integers: callers with string keys hash them, and `mix32` is the
registry's default.

The C port takes an extra `max_keys` parameter and refuses keys beyond it, since
it allocates everything up front and never grows. That is a property of the C
API rather than of the policy, and it is documented in `gcra.h`.
