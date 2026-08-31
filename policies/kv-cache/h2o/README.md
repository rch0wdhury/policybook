# H2O

Keep the tokens that have received the most attention so far, plus a window of
recent ones. This is the first policy in the domain that reads the attention
weights, and the step up from [streaming-llm](../streaming-llm/): that policy
pins the *structurally* special positions, this one finds the *important* ones.

Zhang et al. observed that attention is not merely sparse but **persistently**
sparse: a small set of positions collects most of the attention across a whole
generation, and which positions those are is fairly stable. They called them
heavy hitters, and the policy follows directly: sum each position's attention
over time, and evict the smallest sum.

## When to use it

- **When old tokens carry real information.** A fact stated once in the middle
  of a long document, a system prompt, a retrieved passage: these are exactly
  what recency-plus-sinks policies discard and what this one keeps.
- **When you can see per-step attention weights** and can afford one pass over
  the kept set per step. If your serving stack does not expose them, this policy
  is not available to you at all.
- **When quality matters more than simplicity.** On the canonical trace it
  recovers more of the top-32 attended positions than any policy here that does
  not read attention.
- **As the attention-aware default.** It is the simplest thing that works, and
  the others in this family are refinements of the same idea.

## When not to use it

- **Without checking `recentWindow` against your model's local attention span.**
  This is the one configuration mistake that matters, and it is measured below:
  at the default of 32 on a trace whose recency band is 64 wide, this policy
  retains *less* attention mass than a much simpler one.
- **When attention weights are unavailable or expensive.** The per-step cost is
  O(kept), against O(1) for [streaming-llm](../streaming-llm/), and it needs a
  float64 score per position, 8 bytes each against that policy's zero.
- **When a stale early spike would be actively harmful.** A cumulative sum never
  forgets: a position that mattered enormously once defends its slot forever.
  [scissorhands](../scissorhands/) counts how *often* a position mattered
  instead, which decays that advantage.
- **When simplicity is worth more than a few points.** StreamingLLM is a ring
  buffer with four pinned slots and no arithmetic at all.

## How it works

```
on_decode_step(pos, attn):
    for i, weight in enumerate(attn):      # attn is index-aligned with kept
        score[i] += weight                 # float64
    admit(pos, score = 0)

evict(budget):
    evictable = kept[: len(kept) - recentWindow]
    drop the `len(kept) - budget` lowest-scoring of those
```

Three parallel arrays indexed by kept order, which is the order the attention
arrives in, so no lookup is ever needed on the decode path. Eviction is
repeated argmin rather than a sort, because in steady state exactly one position
goes per step and that makes it a single linear scan.

**The recent window is load-bearing, not a refinement.** A token generated this
step has accumulated nothing, so without protection it would be the first thing
evicted every time and the cache would never advance.

**Scores accumulate in float64 while the weights arrive as float32.** Four
thousand float32 additions would lose low-order bits that the eviction
comparison then depends on. A test pins the exactness over a thousand steps.

**Tie-breaking.** Equal scores evict the lower position. Victims are returned in
ascending position order, not in the order they were chosen.

## Parameters

| Name | Type | Default | Description |
|---|---|---:|---|
| `budget` | number | 512 | Maximum token positions kept in the cache. |
| `recentWindow` | number | 32 | The most recent positions, never evicted whatever their score. |

`recentWindow >= budget` is refused: it would leave the score nothing to choose
between.

### On choosing `recentWindow`

**Set it at least as wide as your model's local attention band.** This is worth
a paragraph because the failure is quiet and because our own benchmark trace
exhibits it. The policy still works, it just gives away mass.

The canonical trace puts 0.55 of its attention on the most recent 64 positions.
At `recentWindow = 32`, positions that are 33 to 64 steps old are unprotected
and must compete on cumulative score: against attention sinks and against heavy
hitters from earlier epochs that have been accumulating for thousands of steps.
They lose. Measured at budget 256:

| `recentWindow` | Retained mass | Heavy-hitter recall |
|---:|---:|---:|
| 16 | 0.4847 | 0.6806 |
| 32 (default) | 0.6439 | 0.8863 |
| **64** | **0.7712** | 0.8823 |
| 128 | 0.7745 | 0.8902 |

The gain saturates at exactly 64, the trace's span, which is what identifies
the window width as the cause rather than something else. At 32 this policy
retains *less* mass than [streaming-llm](../streaming-llm/)'s 0.7367. At 64 it
beats it on both metrics at once.

The default stays at 32 because it follows the paper's shape, and because tuning
a shipped default to our own synthetic benchmark would make the benchmark
meaningless. Tune it to your model, not to this table.

## Complexity

O(kept) time per decode step, one addition per kept position, and O(kept) per
eviction. O(`budget`) space: per slot, 4 bytes of position, 8 of score and 1 of
eviction scratch, so about 6.7 KB at the default budget.

This is the first policy in the domain whose per-step cost grows with the cache.
[streaming-llm](../streaming-llm/) is O(1) per step regardless of budget.

## Benchmark

<!-- bench:start -->
| Trace | Retained mass | Heavy-hitter recall | Throughput |
|---|---:|---:|---:|
| `decode-4096@256` | 0.6439 (−0.1395) | 0.8863 | 45,900/s |
| `decode-4096@512` | 0.7149 (−0.1092) | 0.9137 | 41,300/s |
| `decode-4096@1024` | 0.7844 (−0.1036) | 0.9451 | 35,400/s |

Both columns are proxies for output quality, not measurements of it, so read the domain README before drawing conclusions. They also disagree: the policies leading on retained mass are not the ones leading on heavy-hitter recall, and ranking by either alone will mislead you. Rows are one budget each, because the ordering changes with the budget. Throughput is machine-dependent and is never asserted.

<sub>Generated by `pnpm bench && pnpm render` from core 0.1.0. Do not edit.</sub>
<!-- bench:end -->

## Source

**H2O: Heavy-Hitter Oracle for Efficient Generative Inference of Large Language
Models**. Zhenyu Zhang, Ying Sheng, Tianyi Zhou, Tianlong Chen, Lianmin Zheng,
Ruisi Cai, Zhao Song, Yuandong Tian, Christopher Ré, Clark Barrett, Zhangyang
Wang, Beidi Chen. NeurIPS 2023. <https://arxiv.org/abs/2306.14048>

Its nearest neighbour is [scissorhands](../scissorhands/), which replaces the
cumulative sum with a count of how many steps a position beat its fair share.
The distinguishing vectors in both policies run identical steps: a position that
takes almost the whole of the first two steps and nearly nothing after has the
highest score of any position here and a single vote there, so this policy
defends it and that one evicts it.

On the canonical trace the two land within a hundredth of each other, because
that workload's heavy hitters hold their weight for a whole 512-step epoch and
so score well under either rule. The difference is real but that trace does not
exercise it, which is worth knowing before reading much into the table.

## Notes

No patents known.

The `Rng` is accepted at construction and never used: this policy is entirely
deterministic.

A null `attn` is read as "no information this step" and leaves every score
unchanged, rather than as a vector of zeroes. The distinction matters: zeroes
would let uninformative steps dilute nothing, but would still advance the
comparison between positions on evidence that was never supplied.
