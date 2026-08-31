# Cache traces

The canonical workloads every cache policy is benchmarked on. They are
generated, not downloaded: a port in any language reproduces them event for
event from the seed alone.

This document is the specification. `traces.ts` is the reference
implementation, and `packages/python/policybook/domains/cache/traces.py` and
`packages/c/src/cache/traces.c` must agree with it exactly — the parity tests in
T08 and T09 check the first 10,000 events of each.

## Shared machinery

**Rng** is xoshiro128\*\* seeded by splitmix32, seeded once per
trace with the seed below and never reseeded.

**Zipf sampling** draws a rank from `0 .. keyspace - 1`; the rank *is* the key,
so key 0 is the most popular. Weights are `1 / (rank + 1)^alpha`, accumulated
into a table in ascending rank order. A draw takes exactly one `nextFloat()`:

```
target = nextFloat() * total
low = 0; high = size - 1
while low < high:
    mid = (low + high) >> 1          # integer division
    if cumulative[mid] > target: high = mid
    else:                        low = mid + 1
return low
```

The summation order is part of the specification: adding the weights in any
other order gives a slightly different total and eventually a different key.

**Exponents are 1.0 and 0.75, and no others.** `pow` is not correctly rounded
across C standard libraries, so a trace built on it would not be reproducible.
Both supported exponents need only `sqrt`, which IEEE-754 requires to be
correctly rounded:

| alpha | weight of rank `i`, with `r = i + 1` |
|---|---|
| 1.00 | `1 / r` |
| 0.75 | `s = sqrt(r); q = sqrt(s); 1 / (s * q)` — because `r^0.75 = r^(1/2) · r^(1/4)` |

This is why the concept's `zipf-0.8` ships as `zipf-0.75`.

## The traces

### `zipf-1.0-100k`

The everyday skewed workload, and the one to look at first.

| | |
|---|---|
| Events | 100,000 |
| Keyspace | 10,000 |
| Capacity | 1,000 (10% of the keyspace) |
| Alpha | 1.0 |
| Seed | 42 |

Each event is one Zipf draw.

### `zipf-0.75-1m`

Flatter and ten times longer. A flatter distribution means a larger working set
and lower hit rates for everyone, which spreads the policies out; the length is
enough that per-entry overhead and per-operation cost start to show.

| | |
|---|---|
| Events | 1,000,000 |
| Keyspace | 100,000 |
| Capacity | 10,000 (10%) |
| Alpha | 0.75 |
| Seed | 43 |

### `scan-heavy`

The trace that separates scan-resistant policies from LRU. A sequential sweep of
fresh keys — a backup, a table scan, a crawler — arrives periodically and is
never seen again. LRU dutifully caches all of it and throws away its working set.

| | |
|---|---|
| Events | 108,000 |
| Keyspace | 10,000 (Zipf body) |
| Key universe | 18,000 (scans emit 10,000–17,999) |
| Capacity | 1,000 |
| Alpha | 1.0 |
| Seed | 44 |

Generation, for `step` in `0 .. 99,999`:

1. If `step > 0` and `step % 20,000 == 0`, emit a scan burst first:
   `scanBase + 0 .. scanBase + 1,999`, where
   `scanBase = 10,000 + scanIndex * 2,000` and `scanIndex` counts bursts from 0.
2. Then emit one Zipf draw.

That is 100,000 Zipf events plus four bursts of 2,000, so 108,000 in total. Scan
keys sit above the Zipf keyspace and so can never collide with the working set,
and each scan key is touched exactly once in the whole trace.

### `shifting-popularity`

Popularity is not static, and a policy that only counts frequency will hold
yesterday's winners forever. Here the popular set rotates by 2,500 keys every
25,000 accesses.

| | |
|---|---|
| Events | 100,000 |
| Keyspace | 10,000 |
| Capacity | 1,000 |
| Alpha | 1.0 |
| Seed | 45 |

For each `step`, draw `rank` as usual, then emit

```
key = (rank + floor(step / 25,000) * 2,500) mod 10,000
```

## Metrics

| Metric | Definition |
|---|---|
| `hitRate` | hits / events, rounded to six places, half away from zero |
| `evictions` | entries removed to make room |

Throughput is measured but never asserted: it is machine-dependent and lives in
the `perf` section of `bench.json`.

Rounding is `Math.round(value * 1e6) / 1e6`. Ports must match the mode, not just
the precision: Python's built-in `round` is banker's rounding and would disagree
on an exact tie, so a Python port uses `math.floor(value * 1e6 + 0.5) / 1e6`.
