/**
 * Scenario script for kv-cache/pyramidkv.
 *
 * The selection machinery is SnapKV's and is covered by that policy's vectors.
 * What these pin is the part that is new: the per-layer budget allocation, and
 * the fact that a layer's own share can bind tighter than the budget the caller
 * asks for.
 *
 * **Every allocation case ends in an eviction**, not only in an
 * `effectiveBudget` reading. The C emitter skips introspection-only steps, so a
 * case asserting nothing else would be silently untested in the C port
 *. Three cases below run identical
 * steps at three layer configurations and differ only in what comes out of
 * `evict`, which is exactly the observable the allocation controls.
 *
 * At `budget: 6`, `pyramidRatio: 2`, `numLayers: 3` the sequence is 8, 6, 4 —
 * mean 6, first twice the last, and every term exact under the integer
 * division. Those are the numbers the cases use.
 *
 * Regenerate with: pnpm gen:vectors kv-cache/pyramidkv
 */

import type { ScenarioFile } from "../../../packages/vectors/src/gen";

/**
 * Six steps whose window sums come out strictly ordered by position.
 *
 * Position 0 ends on 2.0 and each later one on less, so "the lowest scorers"
 * is unambiguous and the only thing varying between the cases below is how
 * many of them the layer's budget forces out.
 */
const RAMP: (number[] | null)[] = [
  [1.0],
  [0.5, 0.5],
  [0.25, 0.25, 0.5],
  [0.125, 0.125, 0.25, 0.5],
  [0.0625, 0.0625, 0.125, 0.25, 0.5],
  [0.0625, 0.0625, 0.0625, 0.125, 0.25, 0.4375],
];

const rampSteps = RAMP.map((attn, index) => ({
  call: "onDecodeStep",
  args: [index + 1, attn] as [number, number[] | null],
}));

const scenarios: ScenarioFile = {
  policy: "kv-cache/pyramidkv",
  cases: [
    {
      name: "smoke: one layer redistributes nothing",
      // The default, and the configuration the registry's benchmark runs. With
      // a single layer the allocation returns the budget unchanged and this
      // policy is SnapKV exactly.
      params: { budget: 6, layer: 0, numLayers: 1, recentWindow: 1, obsWindow: 8, poolKernel: 1 },
      seed: 1,
      steps: [
        ...rampSteps,
        { call: "effectiveBudget", args: [], expect: 6 },
        { call: "windowScoreOf", args: [0], expect: 2.0 },
        { call: "windowScoreOf", args: [5], expect: 0.4375 },
        // Seven held against six: one goes, the lowest scorer.
        { call: "evict", args: [6], expect: [5] },
        { call: "keptCount", args: [], expect: 6 },
      ],
    },
    {
      name: "distinguishing: a deep layer's share binds tighter than the ask",
      // The whole policy, in one comparison with the case above. Identical
      // steps, identical budget offered to `evict`, and three positions leave
      // instead of one — because layer 2 of 3 is allocated 4, not 6.
      params: { budget: 6, layer: 2, numLayers: 3, pyramidRatio: 2, recentWindow: 1, obsWindow: 8, poolKernel: 1 },
      seed: 1,
      steps: [
        ...rampSteps,
        { call: "effectiveBudget", args: [], expect: 4 },
        // The caller offers six; the layer's own share is four and wins.
        { call: "evict", args: [6], expect: [3, 4, 5] },
        { call: "keptCount", args: [], expect: 4 },
      ],
    },
    {
      name: "boundary: a shallow layer's share cannot exceed what the caller allows",
      // Layer 0 of 3 is allocated 8, more than the average 6. The caller's
      // budget still caps it, so the eviction matches the single-layer case
      // exactly — a policy that let its own share win here would overrun the
      // cache it was given.
      params: { budget: 6, layer: 0, numLayers: 3, pyramidRatio: 2, recentWindow: 1, obsWindow: 8, poolKernel: 1 },
      seed: 1,
      steps: [
        ...rampSteps,
        { call: "effectiveBudget", args: [], expect: 8 },
        { call: "evict", args: [6], expect: [5] },
        { call: "keptCount", args: [], expect: 6 },
      ],
    },
    {
      name: "the middle layer of three gets exactly the average",
      // An arithmetic sequence is symmetric about its mean, so the middle term
      // is the average whatever the ratio. Worth pinning: it is the property
      // that makes the redistribution a redistribution rather than a change of
      // total.
      params: { budget: 6, layer: 1, numLayers: 3, pyramidRatio: 2, recentWindow: 1, obsWindow: 8, poolKernel: 1 },
      seed: 1,
      steps: [
        ...rampSteps,
        { call: "effectiveBudget", args: [], expect: 6 },
        { call: "evict", args: [6], expect: [5] },
      ],
    },
    {
      name: "tiebreak: a ratio of one is the degenerate uniform pyramid",
      // Every layer gets the average, so the policy is SnapKV at every depth.
      // This is the control that shows the ratio is what drives the sequence
      // rather than the layer index alone.
      params: { budget: 6, layer: 2, numLayers: 3, pyramidRatio: 1, recentWindow: 1, obsWindow: 8, poolKernel: 1 },
      seed: 1,
      steps: [
        ...rampSteps,
        { call: "effectiveBudget", args: [], expect: 6 },
        { call: "evict", args: [6], expect: [5] },
      ],
    },
    {
      name: "a share below the recent window is raised to hold one choosable position",
      // Layer 3 of 4 at ratio 4 is allocated 2*10*(4*3 - 3*3) / (5*3) = 60/15 =
      // 4, which is below the recent window of 4 — the cache would be smaller
      // than its own protected region, and the score would have nothing to
      // choose between. It holds the window plus one instead, so exactly one
      // position is ever at risk.
      params: { budget: 10, layer: 3, numLayers: 4, pyramidRatio: 4, recentWindow: 4, obsWindow: 8, poolKernel: 1 },
      seed: 1,
      steps: [
        ...rampSteps,
        { call: "effectiveBudget", args: [], expect: 5 },
        // Seven held and a target of five, so two go. The four protected
        // positions are 3-6, leaving 0, 1 and 2 to choose between — and since
        // the ramp puts position 0 highest at 2.0, the victims are the two in
        // the middle rather than the two oldest.
        { call: "evict", args: [10], expect: [1, 2] },
        { call: "keptCount", args: [], expect: 5 },
      ],
    },
  ],
};

export default scenarios;
