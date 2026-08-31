# PyramidKV

Spend more cache on early layers than late ones. Every other policy in this
domain decides *which* tokens to keep. This one decides *how many*, and leaves
the choosing to a [snapkv](../snapkv/)-style rule underneath.

Cai et al. called the observation **pyramidal information funnelling**:
attention in the early layers of a transformer is broad and fairly uniform,
spread across the whole context, while in later layers it concentrates sharply
onto a few positions. A uniform per-layer cache budget therefore spends the same
on a layer that needs to see everything and a layer that needs a handful of
tokens, overfeeding the deep layers and starving the shallow ones.

## Read this first: our benchmark cannot test this policy

**On a single-layer workload PyramidKV is exactly SnapKV.** That is arithmetic,
not a hedge: with `numLayers: 1` there is nothing to redistribute and the
allocation returns `budget` unchanged.

This registry's trace is single-layer. So the benchmark rows below are
*identical to SnapKV's, by construction*: not because the two policies were
compared and tied, but because on this workload they are the same program. The
allocation rule is real, is implemented in all three languages, and is covered
by vectors and tests at two, three and four layers. What is missing is a
multi-layer **workload** to run it against, and building one would mean
simulating a whole transformer's attention rather than one layer's.

Treat the row as "SnapKV's numbers, reproduced" and the policy as something to
evaluate on your own model.

## When to use it

- **When you are sizing a real multi-layer KV cache** and have a fixed total
  memory budget to divide. That is the entire purpose, and nothing else here
  addresses it.
- **When your model shows the funnelling pattern**: broad attention early,
  concentrated attention late. It is common but not universal, and it is
  measurable on your own model before you commit to it.
- **When you already wanted SnapKV.** The selection is identical, so adopting
  this costs nothing and leaves the allocation available when you want it.

## When not to use it

- **On a single layer**, where it is SnapKV with extra parameters to
  misconfigure. Use [snapkv](../snapkv/) directly and say what you mean.
- **When you have not measured your model's layer profile.** `pyramidRatio` is
  the whole policy, and this registry has no measurement to offer for it. The
  default of 4 is a starting point, not a recommendation.
- **When memory is tight.** It inherits SnapKV's `obsWindow` floats per slot,
  the heaviest per-position cost in the domain, and a shallow layer allocates
  *more* than the average budget by design.
- **When the funnelling does not hold.** If attention stays broad at depth, this
  starves exactly the layers that needed the cache.

## How it works

```
effective(k) = 2*budget*( r*(L-1) - k*(r-1) ) / ( (r+1)*(L-1) )     # L > 1
             = budget                                                # L == 1

on_decode_step / evict:  as snapkv, but evicting down to
                         min(caller's budget, effective)
```

An arithmetic sequence from `2·budget·r/(r+1)` at the first layer down to
`2·budget/(r+1)` at the last, whose **mean is `budget` by construction**, so
this is a redistribution of a fixed total, not an increase. The middle layer of
an odd stack gets exactly the average, which a vector pins.

It is evaluated as a **single integer division** rather than as a float sequence
rounded per layer, so all three implementations floor at the same point and
allocate identically. In C the numerator is computed in `uint64_t`: it reaches
`2 · budget · ratio · numLayers`, which overflows 32 bits for a large model at a
large budget.

**One instance serves one layer.** A real deployment builds `numLayers` of them,
each with its own `layer`. The allocation is also exported on its own
(`pyramidBudget` in TypeScript and Python, `pb_kvcache_pyramid_budget` in C) so
a caller can size buffers before constructing anything.

**Eviction targets the tighter of the caller's budget and this layer's share.**
A deep layer holds less than it was offered, which is the point. A shallow layer
whose share exceeds the offer is still capped by the offer, because overrunning
the cache you were given is not a trade-off, it is a bug. A vector pins both
directions.

**A policy cannot evict itself, so the caller's cadence decides peak
residency.** This registry's harness asks for an eviction whenever the kept set
passes *its* budget, so a deep layer climbs to that budget, is asked once, and
drops to its own share, sawtoothing rather than sitting at the share. A real
deployment gives each layer its own cache and asks at that layer's budget, and
residency stays put. The retained-mass effect is real either way, because the
deep layer holds fewer positions on average. The *peak memory* saving the paper
is after is simply not observable in a single-layer harness. A test measures
immediately after an eviction for exactly this reason.

**A share below the recent window is raised to `recentWindow + 1`.** A cache
smaller than its own protected region is a state the selection rule cannot
express: the score would have nothing to choose between. So a very deep layer
at an aggressive ratio keeps the window plus one position.

## Parameters

| Name | Type | Default | Description |
|---|---|---:|---|
| `budget` | number | 512 | Average positions kept per layer, before redistribution. |
| `layer` | number | 0 | Which layer this instance serves, counting from the input. |
| `numLayers` | number | 1 | Layers the budget is shared across. One means no redistribution. |
| `pyramidRatio` | number | 4 | How many times more cache the first layer gets than the last. |
| `recentWindow` | number | 32 | The most recent positions, never evicted whatever their score. |
| `obsWindow` | number | 16 | How many recent decode steps the score is summed over. |
| `poolKernel` | number | 7 | Width of the max-pool across neighbouring kept positions. Must be odd. |

`pyramidRatio: 1` is the degenerate uniform pyramid, where every layer gets
the average, and is a useful control. Below 1 is refused: it would invert the
pyramid, which is a different policy and not this one. `layer >= numLayers` is
refused, as are the SnapKV constraints it inherits.

At `budget: 512`, `numLayers: 32`, `pyramidRatio: 4`, the sequence runs from 819
at layer 0 down to 204 at layer 31.

## Complexity

As [snapkv](../snapkv/): O(kept) per decode step, O(kept × `obsWindow` +
kept × `poolKernel`) per eviction, and O(`budget` × `obsWindow`) space sized from
whichever of `budget` and this layer's share is larger.

## Benchmark

<!-- bench:start -->
| Trace | Retained mass | Heavy-hitter recall | Throughput |
|---|---:|---:|---:|
| `decode-4096@256` | 0.7834 | 0.8902 | 31,700/s |
| `decode-4096@512` | 0.8212 (−0.0028) | 0.9019 | 23,400/s |
| `decode-4096@1024` | 0.8880 | 0.9412 | 16,400/s |

Both columns are proxies for output quality, not measurements of it, so read the domain README before drawing conclusions. They also disagree: the policies leading on retained mass are not the ones leading on heavy-hitter recall, and ranking by either alone will mislead you. Rows are one budget each, because the ordering changes with the budget. Throughput is machine-dependent and is never asserted.

<sub>Generated by `pnpm bench && pnpm render` from core 0.1.0. Do not edit.</sub>
<!-- bench:end -->

## Source

**PyramidKV: Dynamic KV Cache Compression based on Pyramidal Information
Funneling**. Zefan Cai, Yichi Zhang, Bofei Gao, Yuliang Liu, Tianyu Liu, Keming
Lu, Wayne Xiong, Yue Dong, Baobao Chang, Junjie Hu, Wen Xiao. arXiv 2024.
<https://arxiv.org/abs/2406.02069>

Its nearest neighbour is [snapkv](../snapkv/), which it reduces to exactly at one
layer, so the distinguishing vectors compare it against *itself* at different
layer configurations rather than against another policy: identical steps and an
identical budget offered to `evict`, where layer 2 of 3 sheds three positions and
a single layer sheds one.

## Notes

No patents known.

The `Rng` is accepted at construction and never used: this policy is entirely
deterministic.
