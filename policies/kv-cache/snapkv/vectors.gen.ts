/**
 * Scenario script for kv-cache/snapkv.
 *
 * `onDecodeStep(pos, attn)` writes this step's attention into each kept
 * position's ring of the last `obsWindow` steps. `evict(budget)` sums each
 * ring, max-pools the sums across `poolKernel` neighbours, and drops the lowest
 * outside the recent window.
 *
 * The two mechanisms are pinned separately, because a failure in either would
 * otherwise look the same from outside. Cases with `poolKernel: 1` disable the
 * pooling and test the window alone; a case with a small `obsWindow` shows a
 * weight actually ageing out; and one case runs the same steps at two kernel
 * widths to show the pooling changing the answer.
 *
 * `windowScoreOf` reports the unpooled window sum, which is what separates
 * "the window is wrong" from "the pooling is wrong" in a failure message.
 *
 * Regenerate with: pnpm gen:vectors kv-cache/snapkv
 */

import type { ScenarioFile } from "../../../packages/vectors/src/gen";

const scenarios: ScenarioFile = {
  policy: "kv-cache/snapkv",
  cases: [
    {
      name: "smoke: with a window longer than the run and no pooling, it sums",
      // Worth pinning as the honest description of the base case: an
      // observation window nothing has fallen out of yet, with pooling off, is
      // exactly h2o's cumulative score.
      params: { budget: 3, recentWindow: 1, obsWindow: 8, poolKernel: 1 },
      seed: 1,
      steps: [
        { call: "onDecodeStep", args: [1, [1.0]] },
        { call: "onDecodeStep", args: [2, [0.25, 0.75]] },
        { call: "onDecodeStep", args: [3, [0.5, 0.25, 0.25]] },
        { call: "windowScoreOf", args: [0], expect: 1.75 },
        { call: "windowScoreOf", args: [1], expect: 1.0 },
        { call: "windowScoreOf", args: [2], expect: 0.25 },
        { call: "evict", args: [3], expect: [2] },
        { call: "keptCount", args: [], expect: 3 },
      ],
    },
    {
      name: "distinguishing: attention ages out of the observation window",
      // With obsWindow 2, only the last two steps count. Position 0 takes 1.0
      // then 0.875 and would be untouchable under a cumulative score; by step 4
      // both of those have fallen out of its ring and it holds 0.125, the
      // lowest of anything evictable. H2O on these same steps evicts position 3
      // instead and keeps position 0 on a cumulative 2.0.
      params: { budget: 3, recentWindow: 1, obsWindow: 2, poolKernel: 1 },
      seed: 1,
      steps: [
        { call: "onDecodeStep", args: [1, [1.0]] },
        { call: "onDecodeStep", args: [2, [0.875, 0.125]] },
        { call: "onDecodeStep", args: [3, [0.0625, 0.4375, 0.5]] },
        // The 1.0 from step 1 has been overwritten: 0.875 + 0.0625.
        { call: "windowScoreOf", args: [0], expect: 0.9375 },
        { call: "evict", args: [3], expect: [2] },
        { call: "onDecodeStep", args: [4, [0.0625, 0.4375, 0.5]] },
        // And now the 0.875 has gone too: 0.0625 + 0.0625.
        { call: "windowScoreOf", args: [0], expect: 0.125 },
        { call: "windowScoreOf", args: [1], expect: 0.875 },
        { call: "windowScoreOf", args: [3], expect: 0.5 },
        { call: "evict", args: [3], expect: [0] },
      ],
    },
    {
      name: "the max-pool changes the victim: without it, position 3 goes",
      // The control for the case below. Identical steps, pooling disabled.
      // Position 3 scores 0.0625, the lowest, and is evicted.
      params: { budget: 4, recentWindow: 1, obsWindow: 8, poolKernel: 1 },
      seed: 1,
      steps: [
        { call: "onDecodeStep", args: [1, [1.0]] },
        { call: "onDecodeStep", args: [2, [0.5, 0.5]] },
        { call: "onDecodeStep", args: [3, [0.0625, 0.0625, 0.875]] },
        { call: "onDecodeStep", args: [4, [0.0625, 0.0625, 0.8125, 0.0625]] },
        { call: "windowScoreOf", args: [0], expect: 1.625 },
        { call: "windowScoreOf", args: [1], expect: 0.625 },
        { call: "windowScoreOf", args: [2], expect: 1.6875 },
        { call: "windowScoreOf", args: [3], expect: 0.0625 },
        { call: "evict", args: [4], expect: [3] },
      ],
    },
    {
      name: "the max-pool changes the victim: with it, position 0 goes",
      // The same steps with a kernel of 3. Position 3 sits next to position 2,
      // which scored 1.6875, so it inherits that and is safe — which is the
      // whole point of the pooling: not to leave a fragment of a phrase behind.
      // Position 0's neighbours are only itself and position 1 (0.625), so it
      // pools to 1.625 and is now the lowest.
      params: { budget: 4, recentWindow: 1, obsWindow: 8, poolKernel: 3 },
      seed: 1,
      steps: [
        { call: "onDecodeStep", args: [1, [1.0]] },
        { call: "onDecodeStep", args: [2, [0.5, 0.5]] },
        { call: "onDecodeStep", args: [3, [0.0625, 0.0625, 0.875]] },
        { call: "onDecodeStep", args: [4, [0.0625, 0.0625, 0.8125, 0.0625]] },
        // The unpooled sums are identical to the case above; only the choice
        // differs, which is what isolates the pooling as the cause.
        { call: "windowScoreOf", args: [3], expect: 0.0625 },
        { call: "evict", args: [4], expect: [0] },
      ],
    },
    {
      name: "boundary: the recent window outranks the pooled score",
      params: { budget: 3, recentWindow: 2, obsWindow: 8, poolKernel: 1 },
      seed: 1,
      steps: [
        { call: "onDecodeStep", args: [1, [1.0]] },
        { call: "onDecodeStep", args: [2, [0.9375, 0.0625]] },
        { call: "onDecodeStep", args: [3, [0.5, 0.25, 0.25]] },
        { call: "windowScoreOf", args: [1], expect: 0.3125 },
        { call: "windowScoreOf", args: [2], expect: 0.25 },
        // Position 2 scores lower than position 1 and survives, because the
        // two-position recent window covers it.
        { call: "evict", args: [3], expect: [1] },
      ],
    },
    {
      name: "tiebreak: equal pooled scores evict the lower position",
      params: { budget: 3, recentWindow: 1, obsWindow: 8, poolKernel: 1 },
      seed: 1,
      steps: [
        { call: "onDecodeStep", args: [1, [1.0]] },
        { call: "onDecodeStep", args: [2, [0.875, 0.125]] },
        { call: "onDecodeStep", args: [3, [0.625, 0.125, 0.25]] },
        { call: "windowScoreOf", args: [1], expect: 0.25 },
        { call: "windowScoreOf", args: [2], expect: 0.25 },
        { call: "evict", args: [3], expect: [1] },
      ],
    },
    {
      name: "a null step is inert, and the eviction shows it",
      // A null attention vector does not advance the ring: the window spans the
      // last obsWindow *observed* steps. Advancing without writing would leave
      // position 0's 1.0 sitting in its slot for another full cycle, so a
      // window claiming to cover the recent past would be summing a weight of
      // indeterminate age.
      //
      // The final eviction is what makes this case worth having. Had the null
      // step advanced the ring, step 3 would have landed on the slot holding
      // the 1.0 and overwritten it, leaving position 0 on 0.125 — the lowest
      // score — and evicted here instead of position 2. Without that the case
      // would be untestable in C, where the introspection steps are skipped and
      // only the eviction is checked.
      params: { budget: 3, recentWindow: 1, obsWindow: 2, poolKernel: 1 },
      seed: 1,
      steps: [
        { call: "onDecodeStep", args: [1, [1.0]] },
        { call: "windowScoreOf", args: [0], expect: 1.0 },
        // Inert: nothing recorded, and the ring does not move on.
        { call: "onDecodeStep", args: [2, null] },
        { call: "windowScoreOf", args: [0], expect: 1.0 },
        { call: "onDecodeStep", args: [3, [0.125, 0.5, 0.375]] },
        // The 1.0 is still in the other slot, so 1.0 + 0.125.
        { call: "windowScoreOf", args: [0], expect: 1.125 },
        { call: "windowScoreOf", args: [1], expect: 0.5 },
        { call: "windowScoreOf", args: [2], expect: 0.375 },
        { call: "evict", args: [3], expect: [2] },
      ],
    },
  ],
};

export default scenarios;
