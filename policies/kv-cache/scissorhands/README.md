# Scissorhands

Count how many steps a token mattered for, rather than how much. A position
earns a vote on every step where its attention exceeds its fair share:
`1 / kept`, what it would get if attention were spread evenly. The fewest
votes is what gets evicted.

Liu et al. called the underlying claim the **persistence of importance**
hypothesis: a token that was influential at one decoding step tends to keep
being influential at later ones, and one that was not, stays uninfluential. If
that holds, reliability is a better signal than magnitude.

## When to use it

- **When a stale spike would otherwise defend a slot forever.** This is the
  whole reason to prefer it to [h2o](../h2o/). A cumulative sum never forgets. A
  vote count effectively does, because everything still relevant keeps voting
  and the one-off does not.
- **When old tokens carry real information** and you can see attention weights,
  the same setting as H2O, which it otherwise closely resembles.
- **When you want the cheaper of the two attention-aware policies.** Votes are
  `uint32`. H2O's scores are `float64`. That is 4 bytes a slot rather than 8,
  about a third less memory overall.
- **When attention magnitudes are noisy but their ranking is not.** A count
  throws away magnitude deliberately, which makes it insensitive to a single
  freak weight in a way a sum is not.

## When not to use it

- **When a genuine one-off fact must be kept.** The policy cannot tell a stale
  early spike from a fact stated once that still matters, and it discards both.
  If your workload has important tokens that are mentioned exactly once,
  [h2o](../h2o/) defends them and this does not. Neither policy can tell those
  two cases apart, and nothing here pretends otherwise.
- **Without checking `recentWindow` against your model's local attention span**,
  the same caveat as H2O's, and measured in its README. At the default of 32 on
  a trace whose recency band is 64 wide, both policies give away attention mass.
- **When attention weights are unavailable or expensive.** O(kept) per step,
  against O(1) for [streaming-llm](../streaming-llm/).
- **When you expected it to differ much from H2O on typical traffic.** On our
  canonical trace the two land within a hundredth of each other. See below.

## How it works

```
on_decode_step(pos, attn):
    share = 1 / len(attn)
    for i, weight in enumerate(attn):     # attn is index-aligned with kept
        if weight > share:                # strictly
            votes[i] += 1
    admit(pos, votes = 0)

evict(budget):
    evictable = kept[: len(kept) - recentWindow]
    drop the `len(kept) - budget` least-voted of those
```

Structurally [h2o](../h2o/) with a vote counter in place of a cumulative score,
which is the entire difference between the two policies.

**The comparison is strict.** A position that exactly matches its fair share
does not vote. At the very first step, where one position holds all the
attention and its share is 1, that means no vote at all, which looks odd and is
correct. This is pinned by a vector and by a test, because "exceeds" and "at
least" are exactly the kind of difference that would otherwise diverge quietly
between the TypeScript, Python and C ports.

**Tie-breaking.** Equal vote counts evict the lower position. That rule does
real work here in a way it does not for H2O: vote counts are small integers, so
ties are common rather than a rare coincidence of floating-point sums. Victims
are returned in ascending position order, not the order they were chosen.

## Parameters

| Name | Type | Default | Description |
|---|---|---:|---|
| `budget` | number | 512 | Maximum token positions kept in the cache. |
| `recentWindow` | number | 32 | The most recent positions, never evicted whatever their vote count. |

`recentWindow` matches H2O's default so the two are directly comparable, and
carries the same caveat: **set it at least as wide as your model's local
attention band.** On the canonical trace, widening it from 32 to 64 raises
retained mass by more than 0.1. H2O's README has the measured table. The effect
here is the same size and has the same cause.

`recentWindow >= budget` is refused: it would leave the votes nothing to choose
between.

## Complexity

O(kept) time per decode step and per eviction. O(`budget`) space: per slot,
4 bytes of position, 4 of votes and 1 of eviction scratch, so about 4.6 KB at
the default budget, roughly a third less than [h2o](../h2o/), whose scores are
float64.

## Benchmark

<!-- bench:start -->
| Trace | Retained mass | Heavy-hitter recall | Throughput |
|---|---:|---:|---:|
| `decode-4096@256` | 0.6413 (−0.1421) | 0.8823 | 42,400/s |
| `decode-4096@512` | 0.7114 (−0.1126) | 0.9060 | 38,700/s |
| `decode-4096@1024` | 0.7847 (−0.1033) | 0.9451 | 34,300/s |

Both columns are proxies for output quality, not measurements of it, so read the domain README before drawing conclusions. They also disagree: the policies leading on retained mass are not the ones leading on heavy-hitter recall, and ranking by either alone will mislead you. Rows are one budget each, because the ordering changes with the budget. Throughput is machine-dependent and is never asserted.

<sub>Generated by `pnpm bench && pnpm render` from core 0.1.0. Do not edit.</sub>
<!-- bench:end -->

## Source

**Scissorhands: Exploiting the Persistence of Importance Hypothesis for LLM KV
Cache Compression at Test Time**. Zichang Liu, Aditya Desai, Fangshuo Liao,
Weitao Wang, Victor Xie, Zhaozhuo Xu, Anastasios Kyrillidis, Anshumali
Shrivastava. NeurIPS 2023. <https://arxiv.org/abs/2305.17118>

Its nearest neighbour is [h2o](../h2o/), and the distinguishing vectors in both
run identical steps at identical budgets. A position that takes almost the whole
of the first two steps and nearly nothing after ends with the highest cumulative
score of any position and exactly one vote: H2O defends it and evicts a younger
position instead, this policy evicts it. A test runs both policies over that
scenario together and asserts they name different victims, so the claim is a
comparison rather than two assertions that could drift apart.

**On the canonical trace the two are within a hundredth of each other** on both
metrics at every budget. That is not a null result but a statement about the
workload: its heavy hitters hold their weight for a whole 512-step epoch, so
they score well under a sum and under a count alike, and the trace contains
nothing that spikes once and vanishes. The difference between these policies is
real, and the vectors demonstrate it, but this workload does not exercise it. A
test pins the closeness so that if a future trace *does* separate them, it will
be noticed rather than assumed.

## Notes

No patents known.

The `Rng` is accepted at construction and never used: this policy is entirely
deterministic.

A null `attn` leaves every vote count unchanged, rather than being read as a
vector of zeroes.
