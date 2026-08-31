# SIEVE

SIEVE is a FIFO queue with one bit per entry and a hand that sweeps it. A hit
sets the bit. When space is needed, the hand moves from old entries toward new
ones: an entry with its bit set has the bit cleared and survives. The first
entry with a clear bit is evicted. The hand stays where it stopped.

That description sounds like [CLOCK](../clock/), and the one difference is the
whole point. **CLOCK moves a survivor to the back of the queue. SIEVE leaves it
exactly where it is.**

Two things follow. An old entry that keeps being used stays near the old end and
is re-examined on every sweep, so it has to keep earning its place rather than
being granted a full new lifetime: *lazy promotion*. And new entries are
inserted at the new end, ahead of the hand, so they can be evicted before
travelling the whole queue: *quick demotion*. One-hit wonders die young, which
is what a web cache otherwise spends most of its capacity on.

## When to use it

- As the default for a web or CDN-shaped cache: lots of objects seen once,
  a small genuinely popular set, and scans passing through. This is the workload
  SIEVE was designed and measured on, and it beats LRU on it.
- When you want scan resistance without the machinery. [ARC](../arc/) and
  [2Q](../2q/) also resist scans, at the cost of ghost lists, tunable sizes and
  considerably more code. SIEVE is one bit and one pointer.
- Under concurrency. Like CLOCK, a hit sets one bit and reorders nothing, so
  readers do not contend.
- When you would otherwise reach for LRU and have not measured anything. SIEVE
  is a strictly simpler implementation with, on typical web traces, a better hit
  rate.

## When not to use it

- When the workload has no one-hit wonders and a stable working set that fits.
  Quick demotion is then solving a problem you do not have, and plain
  [LRU](../lru/) or [CLOCK](../clock/) will match it with a longer track record.
- When popularity is stable and strongly skewed. A single bit cannot rank a key
  used a thousand times above one used twice. [LFU](../lfu/) and
  [W-TinyLFU](../w-tinylfu/) can.
- **When the popular set rotates and the cache is large.** This is a measured
  weakness rather than a theoretical one: on the `shifting-popularity` trace
  SIEVE scores 0.610 against plain [FIFO](../fifo/)'s 0.626. It is the only
  policy in the domain that falls below the FIFO floor anywhere. The visited bit
  is the cause. A key from the *previous* popular set still carries its bit, so
  it survives a sweep it should not, while FIFO discards it on schedule. The
  effect depends on capacity relative to how fast popularity moves: at capacity
  200 SIEVE leads FIFO 0.529 to 0.423, and only from around capacity 1000,
  where the cache spans several rotations, does it fall behind. If your working
  set turns over on a timescale comparable to your cache's, measure before
  assuming SIEVE's usual advantage holds.
- When a newly inserted entry must be guaranteed to survive for a while. SIEVE
  explicitly does not promise this: an entry inserted moments ago can be the
  very next victim if the hand has reached the new end. The `distinguishing`
  vector shows exactly that happening.
- When eviction latency must be bounded per call. As with CLOCK, one eviction
  can sweep the whole cache if every bit is set.
- If you need something older than 2024 for institutional reasons. SIEVE is new,
  and "we use the NSDI paper from last year" is a different conversation from
  "we use CLOCK".

## How it works

Entries sit in insertion order and **never move**. The hand remembers where it
stopped.

```
onAccess(key, hit):
    if hit:  visited[key] = 1           # the entire hit path; nothing moves
    else:    insert key at the new end, visited = 0

evict():
    entry = hand, or the oldest entry if the hand has not started
    while visited[entry]:
        visited[entry] = 0              # one chance, then it is spent
        entry = the next newer entry, wrapping to the oldest past the new end
    hand = the entry just newer than the victim   # retained for next time
    remove entry
```

**Tie-breaking.** Among unvisited entries the hand reaches the oldest first, and
it visits entries strictly in insertion order.

The retained hand is not an optimisation, it is the algorithm: restarting the
sweep at the oldest entry every time would make this CLOCK with extra steps.

## Parameters

| Name | Type | Default | Description |
|---|---|---|---|
| `capacity` | number | 1000 | Maximum number of entries held. |

## Complexity

O(1) amortised for access and eviction. A hit is a single byte write. One
eviction may sweep up to n entries when every bit is set, but each step clears a
bit, so n consecutive evictions cost O(n) in total.

**C memory: 47.8 bytes per entry** at capacity 1000, measured with
`pb_cache_sieve.memory_bytes`: 8 for the key, 1 for the visited bit, 8 for the
two order links, 4 for the free stack, and about 27 for the hash table. Four
bytes more than [CLOCK](../clock/)'s 43.8, which is the cost of the second link:
the hand removes entries from the middle of the order, where CLOCK only ever
removes from the front.

## Benchmark

<!-- bench:start -->
| Trace | Hit rate | Evictions | Throughput |
|---|---:|---:|---:|
| `zipf-1.0-100k` | 0.7273 (−0.0067) | 26,273 | 17,100,000/s |
| `zipf-0.75-1m` | 0.4768 (−0.0203) | 513,203 | 9,560,000/s |
| `scan-heavy` | 0.6686 (−0.0060) | 34,795 | 16,500,000/s |
| `shifting-popularity` | 0.6101 (−0.0786) | 37,992 | 15,500,000/s |

Hit rate is the number that matters, and the bracketed figure is the gap to the best online policy in this domain on that trace. Throughput is machine-dependent and is never asserted.

<sub>Generated by `pnpm bench && pnpm render` from core 0.1.0. Do not edit.</sub>
<!-- bench:end -->

## Source

Zhang, Yang, Yue, Vigfusson and Rashmi, *SIEVE is Simpler than LRU: an Efficient
Turn-Key Eviction Algorithm for Web Caches*, NSDI 2024.

Related: [CLOCK](../clock/) is the same shape and differs only in moving
survivors to the back. The `distinguishing` vectors of both entries pin the
difference. [FIFO](../fifo/) is SIEVE with the bit removed.
[S3-FIFO](../s3-fifo/) attacks the same one-hit-wonder problem with a small
admission queue instead of a hand.

## Notes

No patents.

The paper's pseudocode walks a doubly linked list with `head` as the newest
entry and the hand moving via `prev` toward it. This implementation names the
directions `newer` and `older` instead, because "previous" is ambiguous in a
structure where the hand travels one way and insertion happens at the other end.
The behaviour is identical, and the `distinguishing` and hand-retention vectors
pin it.

SIEVE is an eviction policy, not a cache design: the paper also discusses using
it as a building block inside larger systems, which is out of scope here.
