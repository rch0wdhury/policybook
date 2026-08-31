# ARC

[2Q](../2q/) divides the cache between keys seen once and keys seen twice, and
asks you to choose the division. ARC makes the same division and then learns
where it belongs.

There are four lists. **T1** holds keys seen once recently and **T2** keys seen
at least twice. Behind each sits a ghost list of keys evicted from it, **B1** and
**B2**, holding identifiers and no values. A target `p` says how much of the
cache T1 should get.

The adaptation is the idea, and it is direct. A hit in B1 means *I discarded a
recent key too soon*, so `p` grows and the recent side gets more room. A hit in
B2 means *I discarded a frequent key too soon*, so `p` shrinks. The ghosts are
precisely the evidence needed to tell which mistake was just made, and each one
costs a key rather than a value.

## When to use it

- When the workload changes and you cannot retune. ARC moves its own boundary
  between recency and frequency continuously, with no parameters beyond the
  capacity.
- When you do not know whether your workload is recency-friendly or
  frequency-friendly, and suspect it is both at different times of day.
- As the yardstick. If you are evaluating a new policy, ARC is what reviewers
  will ask you to compare against.
- When scans must not evict the working set. New keys enter T1 and cannot reach
  T2 without a second access, so a scan cannot displace proven entries, the
  same protection [2Q](../2q/) gives, without the fixed split.

## When not to use it

- **Check the patent history first.** See the Notes below. This is not a
  technical objection but it is the first question a legal review will ask, and
  it is the reason several systems shipped [2Q](../2q/) or
  [CLOCK-Pro](../clock-pro/) instead.
- When you want something you can debug at 3am. ARC has four lists, an adaptive
  target and a replacement rule with a special case. Explaining a specific
  eviction takes real effort. [LRU](../lru/) and [SIEVE](../sieve/) do not.
- Under concurrency. Every hit moves an entry between lists, so ARC has LRU's
  write-on-hit problem and more of it. [CLOCK](../clock/) and
  [SIEVE](../sieve/) leave reads pure.
- When memory is tight. The ghost lists double the number of keys tracked, on
  top of four sets of list links per entry.
- When the workload is stable. If your access pattern does not shift, the
  adaptivity is machinery you are paying for and not using.
  [W-TinyLFU](../w-tinylfu/) or [SIEVE](../sieve/) will match it for less.

## How it works

```
onAccess(key, hit):
    if hit:                       # Case I
        move key to the front of T2

    else if key is in B1:         # Case II: recency was undervalued
        p = min(c, p + max(1, |B2| / |B1|))
        REPLACE(key)
        move key from B1 to the front of T2

    else if key is in B2:         # Case III: frequency was undervalued
        p = max(0, p - max(1, |B1| / |B2|))
        REPLACE(key)
        move key from B2 to the front of T2

    else:                         # Case IV: never seen
        if |T1| + |B1| == c:
            if |T1| < c:  drop the oldest of B1; REPLACE(key)
            else:         discard the oldest of T1 outright   # no room for a ghost
        else if |T1| + |B1| < c and |T1|+|T2|+|B1|+|B2| >= c:
            if the total is 2c: drop the oldest of B2
            REPLACE(key)
        insert key at the front of T1

REPLACE(key):
    if |T1| >= 1 and ((key came from B2 and |T1| == p) or |T1| > p):
        move the oldest of T1 to B1        # evicted from the cache
    else:
        move the oldest of T2 to B2
```

Two details are easy to miss. In Case IV, when T1 alone fills the cache and B1
is empty, the oldest entry of T1 is **discarded without leaving a ghost**:
the invariant `|T1| + |B1| ≤ c` leaves nowhere to record it. And the `|T1| == p`
clause in REPLACE gives the frequent side the benefit of the doubt at exactly
the boundary, but only when the key that triggered the replacement came back
from B2.

**Tie-breaking.** Replacement always takes the oldest entry of whichever list it
chooses, and `p` is an integer, so the choice is fully determined.

## Parameters

| Name | Type | Default | Description |
|---|---|---|---|
| `capacity` | number | 1000 | Maximum number of entries held. |

There is deliberately no tuning knob. That is the point of the policy.

## Complexity

O(1) time for access, insertion and eviction: every operation is a constant
number of list splices.

O(n) space, but with a larger constant than most: up to `2c` keys are tracked,
since each of the `c` cache entries may also have a ghost behind it.

**C memory: 95.5 bytes per entry** at capacity 1000, measured with
`pb_cache_arc.memory_bytes`. That is more than twice [LRU](../lru/)'s 46.8 and
about a quarter more than [2Q](../2q/)'s 75.2, and the reason is structural rather
than wasteful: ARC tracks two keys for every entry it caches.

## Benchmark

<!-- bench:start -->
| Trace | Hit rate | Evictions | Throughput |
|---|---:|---:|---:|
| `zipf-1.0-100k` | 0.7248 (−0.0092) | 26,525 | 12,900,000/s |
| `zipf-0.75-1m` | 0.4753 (−0.0218) | 514,723 | 6,800,000/s |
| `scan-heavy` | 0.6687 (−0.0059) | 34,784 | 12,500,000/s |
| `shifting-popularity` | 0.6887 | 30,129 | 12,700,000/s |

Hit rate is the number that matters, and the bracketed figure is the gap to the best online policy in this domain on that trace. Throughput is machine-dependent and is never asserted.

<sub>Generated by `pnpm bench && pnpm render` from core 0.1.0. Do not edit.</sub>
<!-- bench:end -->

## Source

Megiddo and Modha, *ARC: A Self-Tuning, Low Overhead Replacement Cache*,
FAST 2003. Implemented from the paper's Figure 4.

Related: [2Q](../2q/) makes the same split with a fixed boundary.
[LRU](../lru/) and [LFU](../lfu/) are the two extremes ARC interpolates between.
[W-TinyLFU](../w-tinylfu/) reaches comparable hit rates with far less metadata.

## Notes

**Patent.** IBM was granted US patent 6,996,676, *System and method for
implementing an adaptive replacement cache policy*, covering this algorithm. US
utility patents run twenty years from their earliest filing date, and this one
was filed in 2002, so the term has run out. That is a statement about dates
rather than legal advice: if you are shipping ARC commercially, have someone
qualified confirm the status, including any continuations, before relying on it.
The patent is the documented reason several systems chose
[2Q](../2q/) or CLOCK-Pro instead, and it is why ZFS's ARC-derived cache and
PostgreSQL's clock-sweep took the shapes they did.

**Naming.** "ARC" is used loosely for several things. ZFS's ARC is a derivative
with substantial changes. CAR and CART are the authors' later clock-based
variants. And some systems label any two-list adaptive scheme ARC. This entry is
the FAST 2003 algorithm as published.

**The adaptation direction is easy to invert.** A hit in B1 grows `p` and a hit
in B2 shrinks it, and an implementation with these the wrong way round still
runs, still caches, and simply performs worse than LRU on the workloads ARC
exists for. The `distinguishing` and B2 vectors pin both directions with exact
values.
