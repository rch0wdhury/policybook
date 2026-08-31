# TOVA

Drop whichever token the model just stopped looking at. The score is the
attention a position received **on the current step**, and nothing else: no
accumulation, no history, no protected window.

Oren et al. arrived at it from an unusual direction: they argued that a
decoder-only transformer is really a multi-state RNN whose state is the KV
cache, and that capping the cache turns it into an ordinary one. From there the
eviction rule is obvious. At each step the model tells you, through its
attention, which states it is currently using. Drop the least-used.

## When to use it

- **When importance genuinely shifts.** A long document where the model moves
  section to section, a conversation that changes subject, an agent loop moving
  between tasks. Tracking the current step is right and accumulating is wrong.
- **When you want the best retained-attention numbers here.** On the canonical
  trace it beats every other policy in this domain on retained mass at every
  budget, including [h2o](../h2o/) by a wide margin. See below for why.
- **When you do not want to tune a recency window.** There isn't one. That is
  not an omission. See *How it works*.
- **When memory matters and you still want attention-awareness.** One `double`
  per slot, against [snapkv](../snapkv/)'s `obsWindow` floats.

## When not to use it

- **When a token matters intermittently.** One quiet step is enough to lose it.
  A position that the model returns to every fifty steps looks worthless on the
  forty-nine in between, and this policy has no memory to say otherwise.
  [h2o](../h2o/) and [scissorhands](../scissorhands/) exist for exactly that.
- **When you need the best heavy-hitter recall.** It loses to H2O on that metric
  at every budget. The trade is measured below and neither policy dominates.
- **When attention is noisy step to step.** A single freak weight evicts a good
  token outright, where an accumulated score would have absorbed it.
  [snapkv](../snapkv/) smooths over the last few steps for this reason.
- **When attention weights are unavailable.** O(kept) per step and useless
  without weights. [streaming-llm](../streaming-llm/) needs neither.

## How it works

```
on_decode_step(pos, attn):
    for i, weight in enumerate(attn):     # attn is index-aligned with kept
        last[i] = weight                  # assignment, not accumulation
    admit(pos, last = UNOBSERVED)

evict(budget):
    drop the `len(kept) - budget` lowest `last`, skipping UNOBSERVED
```

**There is no recent-window protection, and that is the interesting part.**
Every other scoring policy here needs one, because a token generated two steps
ago has accumulated nothing and would be evicted on a technicality. This policy
does not, because recency is already in the signal: recent tokens attract high
attention *now*, so the current step's weights protect them without a rule.

That is measurable rather than rhetorical. The canonical trace puts 0.55 of its
attention on the most recent 64 positions. Running this policy at a budget of
256 and inspecting the cache afterwards, **all 64 are still held**, and so are
all four attention sinks, with no rule for those either. A test asserts both.

It is also why this policy beats H2O on retained mass by around 0.13. H2O's
recent window is a fixed 32 positions, which leaves the 33rd- to 64th-newest
tokens to compete on cumulative score against sinks and old heavy hitters, and
they lose. This policy sizes its protection from the data instead of from a
parameter, so it cannot be misconfigured that way.

**The one position this cannot cover** is the token just generated: nothing has
attended to it yet, so it has no weight to be ranked on. It is admitted as
*unobserved* and is not a candidate for eviction until the next step gives it a
real weight. That is a refusal to rank on absent evidence, not a recency rule
smuggled back in: scoring the absence as zero would evict every token on the
step it was created and the cache would never advance.

**Tie-breaking.** Equal weights evict the lower position. Victims are returned
in ascending position order, not the order they were chosen.

## Parameters

| Name | Type | Default | Description |
|---|---|---:|---|
| `budget` | number | 512 | Maximum token positions kept in the cache. |

That is the whole configuration surface, which is unusual for an
attention-aware policy and is the direct consequence of not needing a recency
window.

## Complexity

O(kept) time per decode step and per eviction. O(`budget`) space: per slot,
4 bytes of position, 8 of last-attention and 1 of eviction scratch, about
6.7 KB at the default budget, the same as [h2o](../h2o/) and a fraction of
[snapkv](../snapkv/)'s.

## Benchmark

<!-- bench:start -->
| Trace | Retained mass | Heavy-hitter recall | Throughput |
|---|---:|---:|---:|
| `decode-4096@256` | 0.7781 (−0.0052) | 0.8823 | 93,100/s |
| `decode-4096@512` | 0.8240 | 0.9098 | 87,300/s |
| `decode-4096@1024` | 0.8859 (−0.0022) | 0.9372 | 76,400/s |

Both columns are proxies for output quality, not measurements of it, so read the domain README before drawing conclusions. They also disagree: the policies leading on retained mass are not the ones leading on heavy-hitter recall, and ranking by either alone will mislead you. Rows are one budget each, because the ordering changes with the budget. Throughput is machine-dependent and is never asserted.

<sub>Generated by `pnpm bench && pnpm render` from core 0.1.0. Do not edit.</sub>
<!-- bench:end -->

## Source

**Transformers are Multi-State RNNs**. Matanel Oren, Michael Hassid, Nir
Yarden, Yossi Adi, Roy Schwartz. EMNLP 2024.
<https://arxiv.org/abs/2401.06104>

Its nearest neighbour is [h2o](../h2o/), and the two differ by exactly one
thing: assignment versus accumulation. The distinguishing vectors in both run
identical steps: a position that takes almost all of the first three steps and
then goes quiet ends with a cumulative 2.6875, far the highest in the cache, and
a current-step weight tied for lowest. H2O keeps it. This policy evicts it.

The measured trade on the canonical trace, at every budget: this policy wins on
retained attention mass, H2O wins on heavy-hitter recall. Tests pin both
directions, because a benchmark table showing one number per policy invites the
conclusion that one of them is simply better, and neither is.

Its other neighbour is [snapkv](../snapkv/), which on this trace **reduces
exactly to this policy** when its max-pooling is disabled, identical metrics to
the last decimal at every budget. See that policy's README for why.

## Notes

No patents known.

The `Rng` is accepted at construction and never used: this policy is entirely
deterministic.

A null `attn` leaves every record untouched rather than clearing it, matching
the other attention-reading policies here.
