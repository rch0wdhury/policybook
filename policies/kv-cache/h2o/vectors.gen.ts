/**
 * Scenario script for kv-cache/h2o.
 *
 * `onDecodeStep(pos, attn)` adds this step's attention to each kept position's
 * running total, then admits `pos` with a score of zero. `evict(budget)` drops
 * the lowest scorers outside the recent window. The cache starts holding
 * position 0, whose token exists before the first decode step.
 *
 * Every attention value here is a binary fraction — halves down to sixteenths —
 * so every cumulative score is exact in float64 and float32 alike, and the
 * expectations can be added up by hand rather than captured. `scoreOf` steps
 * pin the arithmetic directly instead of inferring it from which position was
 * evicted, which is what makes a wrong accumulation fail here rather than
 * showing up as a mysterious eviction three steps later.
 *
 * Regenerate with: pnpm gen:vectors kv-cache/h2o
 */

import type { ScenarioFile } from "../../../packages/vectors/src/gen";

const scenarios: ScenarioFile = {
  policy: "kv-cache/h2o",
  cases: [
    {
      name: "smoke: the lowest cumulative score goes",
      params: { budget: 3, recentWindow: 1 },
      seed: 1,
      steps: [
        // Held {0}. Attention is all position 0 has, and it is all of it.
        { call: "onDecodeStep", args: [1, [1.0]] },
        { call: "onDecodeStep", args: [2, [0.25, 0.75]] },
        // Scores now: 0 -> 1.25, 1 -> 0.75, 2 -> 0.
        { call: "onDecodeStep", args: [3, [0.5, 0.25, 0.25]] },
        // Scores now: 0 -> 1.75, 1 -> 1.0, 2 -> 0.25, 3 -> 0.
        { call: "scoreOf", args: [0], expect: 1.75 },
        { call: "scoreOf", args: [1], expect: 1.0 },
        { call: "scoreOf", args: [2], expect: 0.25 },
        { call: "scoreOf", args: [3], expect: 0 },
        // Four held against three. Position 3 is protected by the recent
        // window, so the choice is between 0, 1 and 2, and 2 scores lowest.
        { call: "evict", args: [3], expect: [2] },
        { call: "keptCount", args: [], expect: 3 },
      ],
    },
    {
      name: "boundary: the recent window outranks the score",
      // Position 2 scores 0.25 and position 1 scores 0.3125, so on score alone
      // position 2 would go. It is inside the two-position recent window and
      // survives; this is the protection doing something, not decoration.
      params: { budget: 3, recentWindow: 2 },
      seed: 1,
      steps: [
        { call: "onDecodeStep", args: [1, [1.0]] },
        { call: "onDecodeStep", args: [2, [0.9375, 0.0625]] },
        { call: "onDecodeStep", args: [3, [0.5, 0.25, 0.25]] },
        { call: "scoreOf", args: [1], expect: 0.3125 },
        { call: "scoreOf", args: [2], expect: 0.25 },
        { call: "evict", args: [3], expect: [1] },
        // The lower-scoring position is the one still held.
        { call: "scoreOf", args: [2], expect: 0.25 },
        { call: "scoreOf", args: [1], expect: -1 },
      ],
    },
    {
      name: "distinguishing: one early spike defends a position forever",
      // The same steps as scissorhands' distinguishing case, budget and window
      // and all. Position 0 takes almost the whole first two steps and then
      // nearly nothing, and its cumulative score — 2.0625, the highest of any
      // position — keeps it safe. Scissorhands counts *how often* a position
      // mattered instead, gives position 0 a single vote, and evicts it here.
      params: { budget: 5, recentWindow: 2 },
      seed: 1,
      steps: [
        { call: "onDecodeStep", args: [1, [1.0]] },
        { call: "onDecodeStep", args: [2, [0.875, 0.125]] },
        { call: "onDecodeStep", args: [3, [0.0625, 0.5, 0.4375]] },
        { call: "onDecodeStep", args: [4, [0.0625, 0.3125, 0.3125, 0.3125]] },
        { call: "onDecodeStep", args: [5, [0.0625, 0.25, 0.25, 0.25, 0.1875]] },
        // 1.0 + 0.875 + 0.0625 + 0.0625 + 0.0625 = 2.0625.
        { call: "scoreOf", args: [0], expect: 2.0625 },
        { call: "scoreOf", args: [1], expect: 1.1875 },
        { call: "scoreOf", args: [2], expect: 1.0 },
        { call: "scoreOf", args: [3], expect: 0.5625 },
        // Six held against five. Positions 4 and 5 are protected, so the
        // choice is among 0..3 and position 3 scores lowest.
        { call: "evict", args: [5], expect: [3] },
        // Position 0 is still here, on the strength of two early steps.
        { call: "scoreOf", args: [0], expect: 2.0625 },
      ],
    },
    {
      name: "tiebreak: equal scores evict the lower position",
      // Positions 1 and 2 both reach exactly 0.25 — 0.125 twice against 0.25
      // once — and the rule is that the earlier one goes.
      params: { budget: 3, recentWindow: 1 },
      seed: 1,
      steps: [
        { call: "onDecodeStep", args: [1, [1.0]] },
        { call: "onDecodeStep", args: [2, [0.875, 0.125]] },
        { call: "onDecodeStep", args: [3, [0.625, 0.125, 0.25]] },
        { call: "scoreOf", args: [1], expect: 0.25 },
        { call: "scoreOf", args: [2], expect: 0.25 },
        { call: "evict", args: [3], expect: [1] },
      ],
    },
    {
      name: "several victims come back in position order, not in the order chosen",
      // Squeezing five down to two picks position 3 first (score 0.5), then 2,
      // then 1 — but they are returned ascending, because that is what the
      // domain's tie-break rule implies and what a caller can rely on.
      params: { budget: 5, recentWindow: 1 },
      seed: 1,
      steps: [
        { call: "onDecodeStep", args: [1, [1.0]] },
        { call: "onDecodeStep", args: [2, [0.5, 0.5]] },
        { call: "onDecodeStep", args: [3, [0.25, 0.25, 0.5]] },
        { call: "onDecodeStep", args: [4, [0.125, 0.125, 0.25, 0.5]] },
        { call: "scoreOf", args: [1], expect: 0.875 },
        { call: "scoreOf", args: [2], expect: 0.75 },
        { call: "scoreOf", args: [3], expect: 0.5 },
        { call: "evict", args: [2], expect: [1, 2, 3] },
        { call: "keptCount", args: [], expect: 2 },
      ],
    },
    {
      name: "a step with no attention leaves every score untouched",
      // The interface allows a null attention vector. Reading it as zeroes
      // would be wrong in a subtle way — it would let scores be diluted by
      // steps that carried no information at all.
      params: { budget: 3, recentWindow: 1 },
      seed: 1,
      steps: [
        { call: "onDecodeStep", args: [1, [1.0]] },
        { call: "onDecodeStep", args: [2, null] },
        { call: "scoreOf", args: [0], expect: 1.0 },
        { call: "scoreOf", args: [1], expect: 0 },
        { call: "onDecodeStep", args: [3, [0.25, 0.25, 0.5]] },
        { call: "scoreOf", args: [0], expect: 1.25 },
        { call: "evict", args: [3], expect: [1] },
      ],
    },
  ],
};

export default scenarios;
