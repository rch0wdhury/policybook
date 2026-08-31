# SnapKV

Score each position on the attention it received over the last few steps, then
**max-pool those scores across neighbouring positions** before choosing victims.
Two mechanisms, and the second is one nothing else in this domain has.

The pooling is the interesting idea. Li et al. observed that selecting tokens
purely on individual scores fragments the context: the model attends to a
*phrase*, the peak lands on one token of it, and evicting the rest leaves a
fragment that is worse than useless. Letting a high scorer defend its neighbours
keeps the phrase intact.

## When to use it

- **When important tokens come in runs**: a phrase, a code block, a table row,
  a quoted passage. This is the only policy here that will keep the whole span
  rather than its peak.
- **When attention is noisy step to step.** Summing over an observation window
  absorbs a single freak weight that would make [tova](../tova/) evict a good
  token outright.
- **When you want strong retained-attention numbers.** On the canonical trace it
  is at or near the top on retained mass at every budget.

## When not to use it

- **When memory is tight.** This is by far the heaviest policy in the domain:
  `obsWindow` floats per slot, about 45 KB at the defaults against
  [tova](../tova/)'s 6.7 KB for very similar results here. That is the honest
  headline of the comparison.
- **When your workload looks like our benchmark trace**, where one of its two
  mechanisms provably does nothing, as shown below. On a stationary workload
  [tova](../tova/) gets the same answer for a seventh of the memory.
- **When important tokens are genuinely isolated.** Pooling is a bet that
  importance is locally clustered. Where it is not, a high scorer drags in
  neighbours that deserve nothing, and the pooling costs you slots.
- **When attention weights are unavailable.** O(kept × obsWindow) per eviction
  and useless without weights.

## How it works

```
on_decode_step(pos, attn):
    for i, weight in enumerate(attn):
        history[i][slot] = weight         # ring of the last obsWindow steps
    slot = (slot + 1) % obsWindow
    admit(pos, history = zeroes)

evict(budget):
    sums[i]   = sum(history[i])
    pooled[i] = max(sums[i-r .. i+r])     # r = (poolKernel - 1) / 2, clamped
    drop the lowest `pooled` outside the recent window
```

**The window sums are recomputed from the ring at every eviction** rather than
maintained as a running total. Adding and subtracting from a running sum drifts,
and the drift would have to be bit-identical in three languages to stay
reproducible. Summing afresh is `obsWindow` additions per position and has
nothing to get wrong. The slot order is fixed: index 0 upward, not
chronological. That order is arbitrary, pinned, and identical everywhere.

**A null attention vector is entirely inert**: nothing is written and the ring
does not advance, so the window spans the last `obsWindow` *observed* steps
rather than the last `obsWindow` calls. Advancing without writing would leave a
stale weight in its slot for another full cycle, so a window claiming to cover
the recent past would quietly be summing values of indeterminate age. A vector
pins this.

**Tie-breaking.** Equal pooled scores evict the lower position. Victims are
returned in ascending position order.

### On the adaptation

SnapKV in the paper is a **prefill** algorithm. It compresses a long prompt once,
by looking at the attention that the prompt's last few query positions paid to
its keys, pooling, and selecting, a single shot before generation starts.

This registry's harness is a decode-time eviction loop, so the adaptation is:
the observation window is the last `obsWindow` decode steps, and the pooling
runs over adjacent **kept** positions, which after eviction are not necessarily
adjacent tokens. The mechanism is the paper's. The setting is not, and any
comparison here is between this adaptation and the others, not between published
results.

## Parameters

| Name | Type | Default | Description |
|---|---|---:|---|
| `budget` | number | 512 | Maximum token positions kept in the cache. |
| `recentWindow` | number | 32 | The most recent positions, never evicted whatever their score. |
| `obsWindow` | number | 16 | How many recent decode steps the score is summed over. |
| `poolKernel` | number | 7 | Width of the max-pool across neighbouring kept positions. Must be odd. |

`poolKernel` must be **odd**: an even kernel has no centre, so a position's
neighbourhood would be lopsided and the pooling would drift one way along the
sequence. `poolKernel: 1` disables pooling entirely, which is a useful control
and is what several of the vectors use to test the window in isolation.

`recentWindow >= budget` and `obsWindow` of zero are refused.

## What this policy actually buys, on this trace

Two of the three mechanisms turn out to do nothing measurable on the canonical
workload, and saying so plainly is more useful than a benchmark row.

**The observation window makes no difference at all.** `obsWindow` of 1, 4, 16
and 64 give byte-identical metrics at every budget. The reason is a property of
the trace rather than of the policy: it is stationary within a heavy-hitter
epoch, so ranking positions by their attention over the last sixteen steps gives
the same order as ranking them by the latest step alone.

**Which means that with pooling off, this policy reduces exactly to
[tova](../tova/)**, identical to the last decimal at every budget. A test pins
that equality, both because it is a striking fact about the workload and because
two independently written policies agreeing exactly is a decent check on both.

**So the max-pool is the whole of the measurable difference, and it is not
uniformly good.** At budget 256 and 1,024 it helps. At 512 it hurts. A few
thousandths either way, in both directions:

| Budget | `poolKernel: 1` | `poolKernel: 7` |
|---:|---:|---:|
| 256 | 0.7781 | **0.7834** |
| 512 | **0.8240** | 0.8212 |
| 1024 | 0.8859 | **0.8880** |

None of this says the policy is bad. It says our trace does not contain the
structure the policy is built to exploit. Its attention has no phrases: the
heavy hitters are scattered singletons drawn independently, so there is nothing
for a max-pool to hold together. On text, where important tokens genuinely come
in runs, the pooling has something to do. This benchmark cannot tell you how
much, and a synthetic trace built to reward pooling would only be telling you
what it was built to say.

## Complexity

O(kept) per decode step. O(kept × `obsWindow` + kept × `poolKernel`) per
eviction. O(`budget` × `obsWindow`) space: per slot, 4 bytes of position,
4 × `obsWindow` of history, two doubles and a byte of scratch, about 45 KB at
the defaults, roughly seven times [tova](../tova/).

History is stored as float32 rather than float64. The weights arrive as float32,
so this is lossless, and it halves the largest allocation in the domain.

## Benchmark

<!-- bench:start -->
| Trace | Retained mass | Heavy-hitter recall | Throughput |
|---|---:|---:|---:|
| `decode-4096@256` | 0.7834 | 0.8902 | 31,200/s |
| `decode-4096@512` | 0.8212 (−0.0028) | 0.9019 | 23,200/s |
| `decode-4096@1024` | 0.8880 | 0.9412 | 16,500/s |

Both columns are proxies for output quality, not measurements of it, so read the domain README before drawing conclusions. They also disagree: the policies leading on retained mass are not the ones leading on heavy-hitter recall, and ranking by either alone will mislead you. Rows are one budget each, because the ordering changes with the budget. Throughput is machine-dependent and is never asserted.

<sub>Generated by `pnpm bench && pnpm render` from core 0.1.0. Do not edit.</sub>
<!-- bench:end -->

## Source

**SnapKV: LLM Knows What You are Looking for Before Generation**. Yuhong Li,
Yingbing Huang, Bowen Yang, Bharat Venkitesh, Acyr Locatelli, Hanchen Ye, Tianle
Cai, Patrick Lewis, Deming Chen. NeurIPS 2024.
<https://arxiv.org/abs/2404.14469>

Its nearest neighbour is [tova](../tova/), which it reduces to exactly when
pooling is disabled on this trace. Its other neighbour is [h2o](../h2o/), which
is what this becomes as `obsWindow` grows to cover the whole run. A vector pins
that case too, with the window longer than the scenario and pooling off.

## Notes

No patents known.

The `Rng` is accepted at construction and never used: this policy is entirely
deterministic.
