# Rate-limiter traces

The canonical workloads every rate-limiter policy is benchmarked on. They are
generated, not downloaded: a port in any language reproduces them event for
event from the seed alone.

This document is the specification. `traces.ts` is the reference
implementation, and `packages/python/policybook/domains/rate_limiter/traces.py`
and `packages/c/src/rate_limiter/traces.c` must agree with it exactly — the
parity tests check the first 10,000 events of each against a committed
reference.

## Shared machinery

**Rng** is xoshiro128\*\* seeded by splitmix32, seeded once per
trace with the seed below and never reseeded.

**Time** is an integer number of milliseconds, starting at 0. A trace is a list
of arrivals `(t, key)`, non-decreasing in `t`. Every canonical arrival costs one
unit; per-event costs are exercised by hand-written vectors instead, where the
numbers can be reasoned about rather than being arbitrary.

**Arrivals are Bernoulli, not Poisson.** The generator walks the clock one
millisecond at a time and asks `nextFloat() < p`. A Poisson inter-arrival time
needs a logarithm, `log` is not correctly rounded across C standard libraries,
and a single differing ULP would eventually shift an arrival by a millisecond
and split the three ports apart.

**More than one arrival cannot land in the same millisecond.** At most one
Bernoulli trial happens per millisecond per trace, so the highest rate any of
these traces can express is 1,000/s. All three sit well below that.

**Zipf sampling** is the same machinery the cache traces use: a draw takes
exactly one `nextFloat()`, and the rank drawn *is* the key, so key 0 is the most
popular.

## The reference limiter

Benchmarks configure every policy to the same budget: **100 permits per second,
burst 100**. Policies express that differently — permits per window, tokens per
second, an emission interval — and each policy's README states the mapping.

## `steady` — seed 50

60,000 ms. A single key, `0`. One Bernoulli trial per millisecond at
`p = 0.09`.

```
rng = Rng(50)
for t in 0 .. 59999:
    if rng.nextFloat() < 0.09:
        emit(t, key = 0)
```

Exactly 60,000 draws are consumed, one per millisecond, whether or not an
arrival results.

About 90 requests per second against a 100/s limit — deliberately *under* the
line. Nothing should be refused here, and a policy that refuses anyway is
leaking capacity: a fixed window that resets on a hard boundary can still deny
a request that arrives in an unlucky cluster, while a token bucket with any
burst allowance will not. The trace exists to make that difference visible
before any overload is involved.

## `bursty` — seed 51

60,000 ms. A single key, `0`. A 2,000 ms cycle: the first 200 ms are **ON**
with `p = 0.5`, and the remaining 1,800 ms are **OFF**.

```
rng = Rng(51)
for t in 0 .. 59999:
    if t mod 2000 >= 200:
        continue                      # no draw is consumed
    if rng.nextFloat() < 0.5:
        emit(t, key = 0)
```

**A millisecond in the OFF phase consumes no random draw.** This is the pinned
call order. Drawing during the silence and discarding the result would be an
equally reasonable definition and would produce a completely different trace
from the same seed, so it has to be stated rather than left to the
implementer. 6,000 draws are consumed in total, 200 per cycle across 30 cycles.

Roughly 100 arrivals per burst at an instantaneous 500/s, then silence. The
long-run average is about 50/s — half the limit — so a policy that can save up
during the quiet passes almost everything, and one that cannot refuses about
four requests in five during each burst. This is the trace that separates a
token bucket from a leaky bucket, and neither answer is wrong.

## `many-keys` — seed 52

120,000 ms. Keys drawn from a Zipf α = 1.0 body over 10,000 keys. One Bernoulli
trial per millisecond at `p = 0.5`; **the key is sampled only when the trial
produces an arrival**.

```
rng = Rng(52)
zipf = ZipfSampler(10000, alpha = 1.0)
for t in 0 .. 119999:
    if rng.nextFloat() < 0.5:
        key = zipf.sample(rng)        # exactly one further nextFloat()
        emit(t, key)
```

The draw order is pinned: Bernoulli first, Zipf second and only on an arrival.
Sampling a key unconditionally would consume the stream at a different rate and
diverge immediately.

About 60,000 arrivals over two minutes. The point is per-key bookkeeping: a
sliding window log holds every timestamp for every key it has seen, which is
where its memory goes, while a token bucket holds two integers. This is also
the only trace where the fairness metric is meaningful — and it should be read
knowing that popular keys asked more often, so a low score is not by itself
evidence of a bad policy.

## `overload` — seed 53

60,000 ms. A single key, `0`. One Bernoulli trial per millisecond at `p = 0.3`
— **three times the reference limit, sustained for a minute**.

```
rng = Rng(53)
for t in 0 .. 59999:
    if rng.nextFloat() < 0.3:
        emit(t, key = 0)
```

The same generator as `steady`, at a rate no limiter can meet. About 18,000
arrivals against a budget of 6,000.

This trace exists to demonstrate something the other three cannot: **under
sustained overload every correct limiter admits almost exactly the same
traffic.** Rate times time is not a property any policy can improve on, so the
accept rates converge — 0.3373 to 0.3429 across six of the seven policies. A
reader who assumed the policies differ in throughput needs to see that they do
not, because it redirects the choice onto the things that *do* differ: memory
per key, behaviour at a window seam, and how easily the state distributes.

The seventh is [dual bucket](../../../policies/rate-limiter/dual-bucket/), which
admits twice as much — see the domain README. Its burst allowance is its whole
per-minute quota by construction, which is a real property of RPM-style limits
and not a misconfiguration.

## What is measured

- **`acceptRate`** — accepted ÷ events. Alone it cannot rank anything: a policy
  that accepts everything scores best and is not a limiter.
- **`maxBurst100ms`** — the most accepts inside any 100 ms window, where the
  window is `[t - 99, t]` inclusive at both ends. Against a 100/s reference, 10
  means the policy is smoothing and 100 means it let a full second's budget
  through at once.

  **It saturates against the trace's own arrival rate**, which limits what it
  can tell you. At 300 arrivals a second only about 30 can land in any 100 ms
  window, so no policy can score above that however permissive it is — and on
  these traces the figure is dominated by each policy's initial burst
  allowance, which the reference configuration makes identical. Read it as "did
  this policy smooth *below* the arrival rate", not as a ranking.
- **`jainFairness`** — `(Σx)² / (n · Σx²)` over per-key accept counts, for keys
  that appeared at least once. Reported only for `many-keys`. Both sums are
  exact integers and the division is the single floating-point operation in the
  metric, which is what keeps the three ports agreeing to six decimal places.
- **`entriesTracked`** — the largest `stateSize()` seen, when a policy reports
  it. The number that separates a limiter you can run for a million keys from
  one you cannot.
