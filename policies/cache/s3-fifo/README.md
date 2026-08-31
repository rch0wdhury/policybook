# S3-FIFO

On web-shaped workloads most objects are requested exactly once. A cache that
admits everything therefore spends most of its capacity on keys it will never
serve again. [W-TinyLFU](../w-tinylfu/) answers that with a frequency sketch.
S3-FIFO answers it with a **small queue that most objects never leave**.

New keys enter **S**, a FIFO holding a tenth of the cache. An object requested
again while it sits there earns promotion to **M**, the main FIFO. An object
that is not simply falls out, leaving its key in **G**, a ghost queue with no
values behind it. A key that returns while its ghost is live skips the audition
and goes straight into M: falling out of S and coming back is itself evidence
of reuse across a full pass of the small queue.

Inside M a two-bit counter grants up to three second chances, in the manner of
[CLOCK](../clock/). But every queue is still a queue: there is no list to
reorder, and a hit anywhere is a single counter increment.

The paper's title is *FIFO queues are all you need for cache eviction*, and that
is the claim: hit rates competitive with far more elaborate policies, from
three queues and two bits.

## When to use it

- On web, CDN and object-storage workloads, where one-hit wonders dominate the
  miss stream. This is the workload the paper measures and where the design pays.
- Under concurrency. Nothing moves on a hit, so readers need no lock and no list
  surgery, the same property [SIEVE](../sieve/) and [CLOCK](../clock/) have,
  and the reason all three scale where LRU does not.
- When you want scan resistance without a sketch or an adaptive parameter.
  [ARC](../arc/) and [W-TinyLFU](../w-tinylfu/) both get there with
  considerably more machinery.
- When the implementation has to be auditable. Three ring buffers and a
  two-bit counter is a policy you can read in one sitting and reason about
  under load.

## When not to use it

- When the working set turns over faster than the small queue. A key must be
  reused *while in S*, or fall out and return while its ghost is live, to be
  cached properly. A workload whose popular set moves faster than that keeps
  everything churning through S.
- When a burst of writes must all stay readable. Only a tenth of the cache
  accepts new keys, so a burst larger than S evicts its own earlier members.
- When you need exact frequency ordering. Two bits distinguish "unused",
  "used", and "used a lot" and nothing finer. [LFU](../lfu/) and
  [W-TinyLFU](../w-tinylfu/) rank properly.
- When eviction latency must be bounded per call. Draining the small queue can
  promote several entries before finding a victim, and the main queue can spend
  several second chances. The amortised cost is O(1), but a single call is not.
- If you want the simplest thing that resists scans, look at
  [SIEVE](../sieve/) first: one queue, one bit, and no ghosts.

## How it works

```
onAccess(key, hit):
    if hit:
        frequency[key] = min(frequency[key] + 1, 3)      # the entire hit path
    else if key is in G:
        remove the ghost; insert at the head of M with frequency 0
    else:
        insert at the head of S with frequency 0

evict():
    if S is at or over its share:
        while S is not empty:
            entry = tail of S
            if frequency[entry] > 1:  move it to M, keeping its counter
            else:                     record its key in G and evict it
    # otherwise, or if S emptied without evicting:
    while M is not empty:
        entry = tail of M
        if frequency[entry] > 0:  decrement and reinsert at M's head
        else:                     evict it
```

**Tie-breaking.** Both queues are strictly FIFO, so among entries with equal
counters the oldest goes first.

**One adaptation from the paper.** The published `evictS` calls `evictM`
directly when a promotion pushes M over its size, so a single `evict` can remove
more than one object. This interface asks for exactly one victim per call, so
that nested call is omitted: if M ends up over its share, S is by construction
under its own, and the next `evict` takes from M. The sequence of victims is the
same, delivered one per call.

## Parameters

| Name | Type | Default | Description |
|---|---|---|---|
| `capacity` | number | 1000 | Maximum number of entries held. |
| `smallFraction` | number | 0.1 | Fraction of capacity given to the small admission queue. |

The ghost queue holds as many keys as the main queue holds entries, so a key
gets roughly one main-queue lifetime to come back.

## Complexity

O(1) amortised for access and eviction. A hit is one counter increment. A single
eviction may promote several entries out of S or spend several second chances in
M, but each of those steps consumes a counter that a later step cannot spend
again.

O(n) space: a key, a two-bit counter and a queue slot per entry, plus a key and
its list links per ghost.

**C memory: 93.5 bytes per entry** at capacity 1000, measured with
`pb_cache_s3_fifo.memory_bytes`. That is more than [SIEVE](../sieve/)'s 47.8 and
[W-TinyLFU](../w-tinylfu/)'s 65.3, and close to [ARC](../arc/)'s 95.5, which
deserves an explanation, because the algorithm itself is far lighter than the
number suggests.

Two costs dominate, and both are the implementation rather than the design.
The ghost queue needs a key and a hash-table entry per remembered key, as it
does in [2Q](../2q/). And **both rings are sized for the whole cache** rather
than for their nominal share: entries migrate from S to M during eviction, and
before the first eviction every entry is in S, so neither ring can be sized to
its steady-state fraction without risking an overflow. A production
implementation that is willing to bound those transients more tightly would
recover most of the difference. The per-entry *state* S3-FIFO actually needs is
a key and two bits.

## Benchmark

<!-- bench:start -->
| Trace | Hit rate | Evictions | Throughput |
|---|---:|---:|---:|
| `zipf-1.0-100k` | 0.7300 (−0.0039) | 25,999 | 10,800,000/s |
| `zipf-0.75-1m` | 0.4856 (−0.0115) | 504,390 | 4,940,000/s |
| `scan-heavy` | 0.6734 (−0.0012) | 34,277 | 9,970,000/s |
| `shifting-popularity` | 0.6849 (−0.0039) | 30,514 | 10,100,000/s |

Hit rate is the number that matters, and the bracketed figure is the gap to the best online policy in this domain on that trace. Throughput is machine-dependent and is never asserted.

<sub>Generated by `pnpm bench && pnpm render` from core 0.1.0. Do not edit.</sub>
<!-- bench:end -->

## Source

Yang, Zhang, Qiu, Yue and Vinayak, *FIFO queues are all you need for cache
eviction*, SOSP 2023.

Related: [SIEVE](../sieve/) is from the same group and reaches similar ground
with one queue and one bit. [FIFO](../fifo/) is what remains when the small
queue, the ghosts and the counter are all removed. [2Q](../2q/) has the same
three-part shape and predates it by thirty years, using LRU where S3-FIFO uses
FIFO. [W-TinyLFU](../w-tinylfu/) chases the same one-hit wonders with a sketch.

## Notes

No patents known. Check before relying on that.

The name is *Simple, Scalable caching with three Static FIFO queues*. The
"static" matters: unlike [ARC](../arc/), the split between the queues never
moves, which is what keeps the implementation free of the adaptive machinery
that makes ARC hard to reason about.

The comparison with [2Q](../2q/) is worth making deliberately. Both admit on a
second access and keep a ghost queue. 2Q uses an LRU for its main cache and
reorders on every hit. S3-FIFO uses a FIFO with a counter and reorders never.
That difference is the whole point of the paper.
