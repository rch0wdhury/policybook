# 2Q

LRU treats every miss as evidence that a key deserves to be cached. Usually it
is wrong: a key seen once and never again still evicts something useful to make
room for itself. 2Q makes keys audition instead.

A new key enters **A1in**, a small FIFO holding about a quarter of the cache.
When it falls out, its *identifier* is kept in **A1out**, a ghost queue with
keys but no values, so it costs almost nothing. If the key comes back while its
ghost is still there, it has demonstrated reuse, and only then does it enter
**Am**, the main LRU.

The effect is that a scan can never reach Am. Its keys arrive once, pass through
A1in, and leave. The working set is untouched.

## When to use it

- When scans, backups or bulk jobs share a cache with a latency-sensitive
  working set. This is what 2Q was designed for, in a database buffer pool, and
  it remains a good answer.
- When you want scan resistance you can explain. Every part of 2Q is a queue,
  and "a key must be requested twice to be cached properly" is a sentence that
  survives being repeated to a colleague.
- When one-hit wonders dominate the miss stream. The ghost queue costs a key per
  entry and buys you the ability to tell a returning key from a new one.
- When ARC's adaptivity is more machinery than you want. 2Q's split is fixed and
  predictable. [ARC](../arc/) tunes the same boundary automatically and is
  correspondingly harder to reason about during an incident.

## When not to use it

- When you have to tune it and cannot measure. `kin` and `kout` are real knobs
  with real consequences, and the defaults (a quarter and a half of capacity)
  are the paper's suggestion rather than a law. [ARC](../arc/) removes this
  problem by adapting. [SIEVE](../sieve/) removes it by not having the knob.
- When the working set turns over quickly. A key must be requested twice within
  the ghost window to be cached properly, so a workload whose popular set
  changes faster than that keeps everything stuck in A1in.
- When the extra structures are not worth it. 2Q needs two queues, a ghost list
  and a membership map. [SIEVE](../sieve/) and [S3-FIFO](../s3-fifo/) get
  comparable scan resistance from one bit per entry.
- When a burst of repeated accesses should count as reuse. A hit on a key still
  sitting in A1in does *nothing*, deliberately. If your notion of "worth
  caching" is "read twice in a row", 2Q will disagree with you.
- On a very small cache. With capacity 4 the admission queue is a single entry,
  and the audition becomes mostly noise.

## How it works

```
onAccess(key, hit):
    if hit:
        if key is in Am:   move it to the MRU end of Am
        else:              do nothing            # a hit in A1in is not promotion
    else:
        if key is in A1out:  remove the ghost; insert into Am at MRU
        else:                append to A1in

evict():
    if A1in is over its share (or Am is empty):
        victim = head of A1in
        remember victim's key in A1out, dropping A1out's oldest if full
    else:
        victim = LRU end of Am                   # no ghost: it already proved itself
    return victim
```

**Tie-breaking.** A1in is strictly FIFO and Am is strictly LRU. Evictions come
from A1in whenever it is over `kin`, so an entry that has never proven reuse is
always a better victim than one that has.

## Parameters

| Name | Type | Default | Description |
|---|---|---|---|
| `capacity` | number | 1000 | Maximum number of entries held. |
| `kin` | number | 0.25 | Fraction of capacity given to the A1in admission queue. |
| `kout` | number | 0.5 | Fraction of capacity worth of keys remembered in A1out. |

Both fractions floor to at least one entry, so a small cache still has a working
admission queue rather than silently degenerating into LRU.

## Complexity

O(1) time for access, insertion and eviction.

O(n) space, with two parts: the resident entries carry a map entry, two list
links and a queue slot. The ghost queue adds `kout × capacity` keys with no
values behind them. The ghosts are the price of the whole idea, and they are
cheap because a key is much smaller than what it points at.

**C memory: 75.2 bytes per entry** at capacity 1000 with the default fractions,
measured with `pb_cache_2q.memory_bytes`, of which about 28 is the A1out ghost
list and its membership table. Compare [LRU](../lru/) at 46.8 and
[SIEVE](../sieve/) at 47.8: 2Q buys its scan resistance with roughly 60% more
memory, where SIEVE gets comparable resistance for one byte per entry. That
comparison is the honest case for preferring SIEVE in a new system, and the
case for 2Q is its explicitness rather than its footprint.

## Benchmark

<!-- bench:start -->
| Trace | Hit rate | Evictions | Throughput |
|---|---:|---:|---:|
| `zipf-1.0-100k` | 0.7147 (−0.0192) | 27,529 | 23,200,000/s |
| `zipf-0.75-1m` | 0.4650 (−0.0321) | 525,009 | 10,800,000/s |
| `scan-heavy` | 0.6587 (−0.0158) | 35,856 | 20,300,000/s |
| `shifting-popularity` | 0.6792 (−0.0095) | 31,082 | 20,600,000/s |

Hit rate is the number that matters, and the bracketed figure is the gap to the best online policy in this domain on that trace. Throughput is machine-dependent and is never asserted.

<sub>Generated by `pnpm bench && pnpm render` from core 0.1.0. Do not edit.</sub>
<!-- bench:end -->

## Source

Johnson and Shasha, *2Q: A Low Overhead High Performance Buffer Management
Replacement Algorithm*, VLDB 1994. This entry implements Full 2Q, the version
with A1in, A1out and Am. The paper also gives a simplified variant with a single
tunable parameter.

Related: [LRU](../lru/) is what 2Q protects against scans.
[ARC](../arc/) adapts the same boundary between recency and proven reuse rather
than fixing it. [SIEVE](../sieve/) and [S3-FIFO](../s3-fifo/) reach similar
scan resistance with far less structure.

## Notes

No patents.

"2Q" is sometimes used loosely for any two-queue scheme, including the
simplified version from the same paper and various segmented-LRU designs. This
entry is Full 2Q as published.

The one detail implementations most often get wrong is the hit in A1in doing
nothing. Reordering or promoting on that hit turns 2Q into an ordinary
segmented LRU and quietly removes the property the algorithm exists for. The
`a hit inside A1in does not promote or reorder` vector pins it.
