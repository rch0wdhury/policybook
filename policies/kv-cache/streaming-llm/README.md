# StreamingLLM

A sliding window that also pins the first few tokens of the sequence. Those
tokens are **attention sinks**: a trained transformer sends a large, roughly
content-independent share of every attention distribution to them, not because
they matter but because softmax has to put its mass somewhere and the earliest
positions are visible from everywhere.

Xiao et al. found that evicting them degrades generation *immediately* rather
than gradually: at the step the first sink goes, not slowly as old context is
lost. A [sliding window](../sliding-window/) evicts them first, because they are
the oldest thing it holds. Keeping four fixes it.

This is the recommended default for the domain: it costs four cache slots and no
arithmetic, and it recovers most of the quality a cheap policy can recover.

## When to use it

- **As the default.** If you are not sure which policy you want, this one. It is
  strictly better than a plain sliding window at a cost of four positions, and
  it has no tuning surface worth arguing about.
- **Streaming and long-running generation**, which is the setting the paper
  addresses: a chat session or agent loop that decodes far past the context
  window and must stay coherent indefinitely.
- **When attention weights are unavailable or too expensive to score.** This
  policy reads none, so it works in serving stacks that do not expose them and
  costs nothing per kept position.
- **When you need a hard, predictable memory bound** with no scoring pass, no
  accumulation, and no per-step cost that grows with the cache.

## When not to use it

- **When old tokens carry information the model genuinely needs.** This policy
  finds the *structurally* special positions, not the important ones. A fact
  stated once in the middle of a long document is evicted exactly as a plain
  window would evict it. [h2o](../h2o/) and its neighbours exist for that case.
- **When you can afford to score attention and quality is worth more than
  simplicity.** Pinning four positions is a fixed, blunt heuristic. Measuring
  which tokens are actually attended to does better on a workload with
  scattered important tokens.
- **On models without attention sinks.** The effect is a property of trained
  transformers rather than a law. If a model does not exhibit it, the four
  pinned slots are four slots wasted, and this degenerates to a slightly smaller
  sliding window.
- **When `sinks` would be a large fraction of `budget`.** At a small budget the
  pinned positions crowd out the recency window that carries most of the mass.
  The paper's four against a budget in the hundreds is the intended shape.

## How it works

```
on_decode_step(pos):  if pos < sinks: sinks_held += 1
                      else:           window.push_back(pos)

evict(budget):        while sinks_held + len(window) > budget and window:
                          yield window.pop_front()
```

The sliding window's ring buffer with the sinks held outside it. Because the
sinks are exactly the positions below `sinks`, tracking them costs a counter
rather than storage, and the ring needs only `budget - sinks + 1` slots. O(1)
per step, nothing allocated after construction.

The cache starts holding position 0, whose token exists before the first decode
step (see the domain interface). It is a sink whenever any are configured.

**Tie-breaking.** Victims come back oldest-first, and sinks are never victims.
When the budget cannot be met without evicting a sink, the policy evicts the
whole window and stops rather than touching one. The constructor refuses
configurations where that could happen in normal operation.

## Parameters

| Name | Type | Default | Description |
|---|---|---:|---|
| `budget` | number | 512 | Maximum token positions kept in the cache. |
| `sinks` | number | 4 | How many of the first positions are pinned and never evicted. |

**Four sinks, from the paper.** Xiao et al. measure the recovery as a function
of this number and find it flat from about four onward: the first token carries
most of the sink mass, and by the fourth there is very little left to recover.
Raising it spends cache on nothing.

`sinks` of 0 is legitimate and makes this a plain sliding window. A vector pins
that equivalence, because it is the honest statement of what the sink count
buys. `sinks >= budget` is refused: it would leave no room for a recency window,
and the policy would behave nothing like the paper.

## Complexity

O(1) time per decode step and per eviction. O(`budget`) space:
`budget - sinks + 1` positions of `uint32_t` in C, so 2,036 bytes at the
defaults plus the struct, very slightly *less* than a plain sliding window,
since the pinned positions need no storage.

## Benchmark

<!-- bench:start -->
| Trace | Retained mass | Heavy-hitter recall | Throughput |
|---|---:|---:|---:|
| `decode-4096@256` | 0.7367 (−0.0467) | 0.8594 | 101,000/s |
| `decode-4096@512` | 0.7862 (−0.0379) | 0.8835 | 101,000/s |
| `decode-4096@1024` | 0.8662 (−0.0218) | 0.9276 | 95,000/s |

Both columns are proxies for output quality, not measurements of it, so read the domain README before drawing conclusions. They also disagree: the policies leading on retained mass are not the ones leading on heavy-hitter recall, and ranking by either alone will mislead you. Rows are one budget each, because the ordering changes with the budget. Throughput is machine-dependent and is never asserted.

<sub>Generated by `pnpm bench && pnpm render` from core 0.1.0. Do not edit.</sub>
<!-- bench:end -->

## Source

**Efficient Streaming Language Models with Attention Sinks**. Guangxuan Xiao,
Yuandong Tian, Beidi Chen, Song Han, Mike Lewis. ICLR 2024, preprint September
2023. <https://arxiv.org/abs/2309.17453>

Its nearest neighbour is [sliding-window](../sliding-window/), which differs by
exactly the pinned positions. The distinguishing vectors in both policies run
identical steps at identical budgets: the window evicts positions 0, 1 and 2 in
turn. This policy keeps all four sinks and evicts 4, 5 and 6 instead.

The next step up is [h2o](../h2o/), which reads attention and so can find old
tokens that are important rather than merely early.

## Notes

No patents known.

The `Rng` is accepted at construction and never used: this policy is entirely
deterministic. The attention argument is not a parameter of the TypeScript
`onDecodeStep` at all, which is the clearest way to state that this policy
cannot read it. A vector supplies attention anyway and pins that the decisions
do not change.
