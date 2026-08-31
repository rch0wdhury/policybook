# W-TinyLFU

[LFU](../lfu/) keeps an exact count for every cached entry. That is expensive,
and it never forgets, so yesterday's popular key holds its place forever.
W-TinyLFU keeps *approximate* counts for far more keys than it caches, in a
fixed-size sketch, and halves them periodically. Frequency information becomes
cheap enough to use for **admission**, deciding whether a key deserves to be
cached at all, rather than only for eviction.

The cache has two parts. A small **window** LRU, one percent of capacity,
absorbs new arrivals so a burst of related requests still hits. When the window
overflows, its victim becomes a *candidate*, and its estimated frequency is
compared with that of the main cache's next victim. Only the more popular of the
two survives. The main cache is a segmented LRU: entries arrive on **probation**
and are promoted to **protected** on a second hit, so proven keys sit furthest
from eviction.

Two tricks make the sketch nearly free. Counters are four bits, two to a byte:
15 means "very popular" and nothing needs more. And a **doorkeeper** bloom filter
absorbs each key's first appearance, so the enormous number of keys seen exactly
once never consume a counter.

## When to use it

- When you want the best hit rate available for the metadata budget. On the
  registry's Zipf trace it beats LRU by around six points, and it does so on the
  scan trace too.
- When one-hit wonders dominate the miss stream. Admission control is the direct
  answer: an unpopular key is *never admitted*, rather than admitted and then
  evicted after displacing something useful.
- When popularity is skewed but drifts. The periodic halving means a key that
  was hot an hour ago decays, which is exactly what [LFU](../lfu/) cannot do.
- When memory per entry matters. The sketch is a few bits per tracked key, and
  it tracks eight positions per cached entry, so frequency knowledge costs far
  less than [LFU](../lfu/)'s exact counters or [ARC](../arc/)'s ghost lists.

## When not to use it

- When keys are not integers and you cannot hash them yourself. The sketch
  hashes keys directly. See Notes.
- When you need to explain a specific eviction. The decision depends on a
  count-min estimate that may be inflated by hash collisions, so "why was this
  key dropped" has no crisp answer. [LRU](../lru/) and [SIEVE](../sieve/) do.
- When the working set fits comfortably. Admission control solves a problem you
  do not have, and the sketch is pure overhead.
- When the workload is a small number of keys each accessed enormously often.
  Counters saturate at 15, so beyond that everything ties, and ties go to the
  incumbent.
- When a newly written key must be readable immediately. It will be, but only
  from the 1% window, and a burst of writes can push each other out before any
  reaches the main cache.
- Under heavy concurrency, in this form. Every access updates the sketch and
  moves list entries. Production implementations (Caffeine) buffer these updates
  and apply them in batches. That machinery is out of scope here and the
  registry's version is the algorithm, not the engineering around it.

## How it works

```
onAccess(key, hit):
    record(key)                        # every access is evidence, hit or miss
    if hit:
        window entry     -> move to the window's front
        probation entry  -> promote to protected; demote protected's oldest if full
        protected entry  -> move to protected's front
    else:
        insert at the window's front
        while the window is over size and the main cache has room:
            move the window's oldest into probation

evict():
    if the window is over size:                    # the admission contest
        candidate = the window's oldest
        victim    = probation's oldest, else protected's oldest
        if estimate(candidate) > estimate(victim):
            move candidate into probation; evict victim
        else:
            evict candidate                        # never admitted at all
    else:
        evict the main cache's victim

record(key):                            # TinyLFU with a doorkeeper
    if the doorkeeper has not seen key:  add it        # first sighting is free
    else:                                increment all four sketch rows
    every 10 x capacity accesses: halve every counter and clear the doorkeeper

estimate(key):
    min over the four rows, plus one if the doorkeeper has seen the key
```

**Tie-breaking.** The candidate is admitted only on a *strictly* greater
estimate. A resident entry has demonstrated its frequency while the candidate
has only an estimate, and admitting on equal evidence would let a stream of
one-hit wonders churn the cache. The `tiebreak` vector pins this.

## Parameters

| Name | Type | Default | Description |
|---|---|---|---|
| `capacity` | number | 1000 | Maximum number of entries held. |
| `windowFraction` | number | 0.01 | Fraction of capacity given to the admission window. |
| `protectedFraction` | number | 0.8 | Fraction of the main cache reserved for protected entries. |

The window is at least one entry however small the cache, and the capacity must
be at least 2 so the window and main cache each have somewhere to put an entry.

## Complexity

O(1) time for access and eviction: four sketch reads or writes, two bloom bits,
and a constant number of list splices. The periodic halving is O(sketch), paid
once every `10 × capacity` accesses, so it amortises to a small constant.

O(n) space. The sketch is `4 rows × 8 × capacity` four-bit counters, four bytes
per cached entry, plus one bloom bit per sketch position.

**C memory: 65.3 bytes per entry** at capacity 1000, measured with
`pb_cache_w_tinylfu.memory_bytes`. More than [LRU](../lru/)'s 46.8 and
[SIEVE](../sieve/)'s 47.8, less than [ARC](../arc/)'s 95.5, and unlike ARC's,
most of the difference buys frequency knowledge about keys the cache does not
hold.

## Benchmark

<!-- bench:start -->
| Trace | Hit rate | Evictions | Throughput |
|---|---:|---:|---:|
| `zipf-1.0-100k` | 0.7339 | 25,605 | 6,430,000/s |
| `zipf-0.75-1m` | 0.4971 | 492,903 | 3,970,000/s |
| `scan-heavy` | 0.6746 | 34,146 | 6,090,000/s |
| `shifting-popularity` | 0.6597 (−0.0290) | 33,028 | 5,900,000/s |

Hit rate is the number that matters, and the bracketed figure is the gap to the best online policy in this domain on that trace. Throughput is machine-dependent and is never asserted.

<sub>Generated by `pnpm bench && pnpm render` from core 0.1.0. Do not edit.</sub>
<!-- bench:end -->

## Source

Einziger, Friedman and Manes, *TinyLFU: A Highly Efficient Cache Admission
Policy*, ACM Transactions on Storage, 2017. The windowed variant (W-TinyLFU) is
from the same line of work and is what Caffeine implements.

Related: [LFU](../lfu/) is the exact, un-aged version this replaces.
[ARC](../arc/) reaches comparable hit rates using ghost lists instead of a
sketch. [SIEVE](../sieve/) and [S3-FIFO](../s3-fifo/) chase the same one-hit
wonders with a single bit and no sketch at all.

## Notes

No patents known. Check before relying on that.

**Keys must be integers.** The sketch hashes keys directly, and a string key
would need a string hash specified identically in TypeScript, Python and C, a
real cost for no benefit, since callers with other key types can hash their own
and are already required to for the C API. This is why the vectors here use
integer keys where other cache policies use strings.

**The estimate can be wrong, and that is the design.** A count-min sketch never
underestimates but can overestimate on hash collisions, so an unpopular key can
occasionally be admitted. Being wrong occasionally, cheaply, beats being right
always at LFU's price.

**This is the algorithm, not Caffeine.** Production implementations add batched
update buffers, adaptive window sizing and a hierarchical timer wheel. Those are
engineering around the policy rather than the policy, and puts
them out of scope.
