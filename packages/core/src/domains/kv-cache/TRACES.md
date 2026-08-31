# KV-cache attention traces

The synthetic workload every kv-cache policy is benchmarked on. It is
generated, not recorded: a real trace would be gigabytes and would tie the
registry to one model, and this way a port in any language reproduces it **bit
for bit** from the seed alone.

This document is the specification. `traces.ts` is the reference
implementation, and `packages/python/policybook/domains/kv_cache/traces.py` and
`packages/c/src/kv_cache/traces.c` must agree with it exactly — the parity tests
compare the float32 bit patterns of the first ten steps and a hash of all
4,095.

## Read this before the numbers

**Every metric in this domain is a proxy.** The harness measures how much of the
model's attention a policy kept. It does not measure output quality, because
measuring that means running a model, and a registry that shipped one could not
be reproduced by anyone in a second. The two correlate — a policy that discards
the tokens the model was looking at produces worse text — but a correlation is
not a guarantee.

**And the workload is synthetic.** It is a caricature of attention built from
four components, chosen because the policies in this domain each exploit one of
them. That makes it a fair *comparison*, since no policy was tuned on it and all
four components are present. It does not make it a prediction: real attention is
messier, task-dependent, and varies by layer and head.

Use these numbers to narrow a shortlist. Confirm the choice against your own
model and your own prompts before shipping it. The papers behind each policy —
linked from the policy pages — evaluate on real generations, which is what this
cannot do.

## `decode-4096` — seed 7

A 4,096-token sequence: positions `0 .. 4095`. At step `t` the token at
position `t` attends over positions `0 .. t-1`, so step `t` yields `t` weights
that sum to exactly one. Steps run from 1 to **4,095** — position 0's token
exists before decoding starts and never attends, so an N-token sequence has
N−1 decode steps, not N.

Four components, each with a fixed share of the mass:

### Sinks — 0.15, on positions 0–3

Split `[0.06, 0.045, 0.03, 0.015]`, heaviest first. When fewer than four
positions exist the available weights are scaled so the component still
contributes exactly 0.15.

Real transformers dump a large, content-independent share of attention on the
first few tokens. [StreamingLLM](../../../../policies/kv-cache/streaming-llm/)
is built entirely on that observation, and this component is why keeping four
positions is worth about eight points of retained mass.

### Local — 0.55, on the last 64 positions

Offsets `d = 1 .. min(64, t)` back from `t`, with weight proportional to
`65 - d` — so the immediately preceding token is heaviest and the 64th-back
lightest.

Recency dominates real attention, which is why a plain sliding window is a
respectable baseline rather than a straw man.

### Heavy hitters — 0.25, on 32 scattered positions

A set `H` of 32 distinct positions drawn from `[4, t - 65]`, weighted by **draw
order**: the first drawn gets `1/1`, the second `1/2`, the *k*-th `1/k`,
normalised to 0.25.

```
draw:  position = rng.nextInt(t - 68) + 4      # yields [4, t - 65]
       redraw on a duplicate, keeping the order of first appearance
```

**`H` is drawn at `t == 128` and redrawn whenever `t` is a multiple of 512.**
Both conditions are pinned. Before 128 there is no room between the sinks and
the recency window for 32 distinct positions, so below that step **the heavy
mass is added to the local component instead** — the total stays at one, and the
switch is visible in the trace at exactly `t = 128`, where local drops from
0.825 to 0.575.

The periodic redraw is what separates a policy that adapts from one that latches
onto an early winner: a policy accumulating attention scores forever will keep
defending positions that stopped mattering 512 steps ago.

### Noise — 0.05, uniform

Spread over every position `0 .. t-1`, so nothing is ever exactly zero and a
policy cannot identify the components by testing for it.

## Determinism

This is the only domain whose trace is floating-point, so it is the only one
where bit-exactness across languages needs stating carefully.

**The output is float32, and that is what buys bit-exactness.** Everything is
computed in float64; after accumulation, one division per position normalises by
the total, and one `fround` — a C `(float)` cast, a Python `struct` round trip —
takes the result to float32. Float32 resolves about one part in 10⁷, while
float64 arithmetic noise is around one part in 10¹⁶, so nine orders of magnitude
separate the two. Differences in how the float64 sum is reached are absorbed by
that final rounding rather than surviving it.

That margin was measured rather than assumed, by perturbing the C generator and
rerunning the parity test:

| Change to the C generator | Output bits |
| --- | --- |
| One sink weight moved by one float64 ULP | unchanged |
| Normalisation total summed descending instead of ascending | unchanged |
| Normalisation removed entirely | unchanged |
| Heavy component switched on at `t = 129` instead of `128` | **caught** |
| Heavy set redrawn every 1,024 steps instead of 512 | **caught** |

So the parity test is a check on the *algorithm* — which steps draw, how many
draws, which positions, what shape the weights are — and not a check on float64
last bits, which it cannot be and does not need to be.

The three disciplines below are therefore cheap insurance rather than the thing
holding parity together. They are kept because they cost nothing, they remove a
class of risk instead of arguing about its size, and they would matter directly
if these weights were ever exposed as float64.

**Contributions are accumulated in a fixed order: sink, local, heavy, noise.**
Float addition is not associative, so a different order can change the float64
result by an ULP. Within local, offsets ascend; within heavy, draw order.

**The normalisation stays** even though removing it changes no output bit. The
components already sum to one to within float64 rounding; dividing by the total
makes that exact in float64 too, which is the invariant the generator is easiest
to reason about.

**No transcendental functions anywhere.** Weights are `65 - d` (integer) and
`1/(rank+1)` (one division). No `pow`, no `exp`, no `log`: none of them are
correctly rounded across C standard libraries.
The measured margin above suggests a 1-ULP libm difference would be absorbed
like any other, but "suggests" is doing real work in that sentence — the ban
costs nothing here, since no formula in this generator wants a transcendental.

**The Rng is consumed only by the heavy-hitter draw**, eight times across the
run (at `t` = 128, 512, 1024, 1536, 2048, 2560, 3072, 3584), plus whatever the
rejection sampling costs. Nothing else draws.

**Python rounds explicitly, and is tested for it.** TypeScript yields a
`Float32Array` and C a `float *`, so both are float32 by construction; Python
has no float32 type, so `traces.py` rounds each weight itself. The bit-pattern
comparison cannot notice if it stops — reading a weight's bits packs it to
float32 anyway — so a separate test asserts every yielded weight survives a
float32 round trip unchanged.

## The committed reference

`trace-prefix.json` holds two things:

- the **float32 bit patterns** of the first ten steps, as unsigned integers.
  Comparing bits rather than values is what makes the check exact: two floats
  that print alike can differ in the last place, and for a generator whose whole
  job is reproducibility that difference *is* the bug.
- an **FNV-1a 32 hash** over the bit patterns of all 4,095 steps, little-endian.
  Committing the whole trace would mean eight million floats; the hash says the
  same thing in one number, and the first steps say where a divergence starts.

## What is measured

- **`retainedAttentionMass`** — the mean share of the model's attention that
  survived, over all steps. The interesting range is narrow, because most
  attention is recent and every policy keeps recent tokens. Read the differences,
  not the absolute figures.
- **`heavyHitterRecall`** — the mean share of the 32 most-attended positions
  still held. This is the metric that separates the policies: the heavy hitters
  are scattered and old, and finding them is the entire reason the
  attention-aware policies exist.
- **`evictionCalls`** — how often the policy was asked. It is a function of the
  budget rather than of the policy (`4096 - budget`), and is reported so a
  reader can see that rather than wonder.

### On the attention the policy is shown

**The cache starts holding position 0** — its token exists before the first
decode step — so the first call already shows the policy one weight, and a
policy must begin its own bookkeeping with position 0 held. Without this no
policy could ever retain the heaviest sink, and even an unbounded budget could
not reach a retained mass of one.

At each step the policy receives the generated weights **restricted to the
positions it still holds, and not renormalised**. They therefore sum to less
than one by exactly the mass it has already lost.

That is deliberate. A policy shown renormalised weights could not tell a
well-preserved cache from a badly damaged one — every step would look the same —
and `retainedAttentionMass` measures precisely the quantity renormalising would
erase.
