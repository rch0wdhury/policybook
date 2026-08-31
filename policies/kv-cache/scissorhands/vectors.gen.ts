/**
 * Scenario script for kv-cache/scissorhands.
 *
 * `onDecodeStep(pos, attn)` gives a vote to every kept position whose attention
 * strictly exceeds `1 / attn.length`, then admits `pos` with no votes.
 * `evict(budget)` drops the fewest-voted positions outside the recent window.
 * The cache starts holding position 0, whose token exists before the first
 * decode step.
 *
 * Every attention value is a binary fraction, and the vote counts are small
 * integers, so the expectations are countable by hand rather than captured.
 * `votesOf` steps pin the voting rule directly — including the strictness of
 * the comparison, which is the single most likely thing to diverge quietly
 * between ports.
 *
 * Regenerate with: pnpm gen:vectors kv-cache/scissorhands
 */

import type { ScenarioFile } from "../../../packages/vectors/src/gen";

const scenarios: ScenarioFile = {
  policy: "kv-cache/scissorhands",
  cases: [
    {
      name: "smoke: the fewest votes goes",
      params: { budget: 3, recentWindow: 1 },
      seed: 1,
      steps: [
        // Held {0}, share = 1/1 = 1. Position 0 receives all the attention and
        // still does not vote, because 1.0 does not *exceed* 1.0.
        { call: "onDecodeStep", args: [1, [1.0]] },
        { call: "votesOf", args: [0], expect: 0 },
        // Share = 1/2. Only 0.75 clears it.
        { call: "onDecodeStep", args: [2, [0.25, 0.75]] },
        // Share = 1/3. Only 0.5 clears it, and that is position 0.
        { call: "onDecodeStep", args: [3, [0.5, 0.25, 0.25]] },
        { call: "votesOf", args: [0], expect: 1 },
        { call: "votesOf", args: [1], expect: 1 },
        { call: "votesOf", args: [2], expect: 0 },
        // Four held against three; position 3 is protected, and 2 never voted.
        { call: "evict", args: [3], expect: [2] },
        { call: "keptCount", args: [], expect: 3 },
      ],
    },
    {
      name: "boundary: exactly matching the fair share earns nothing",
      // The strictness of the comparison, on its own. Two positions split a
      // step evenly and each receives precisely 1/2; neither votes. A port
      // using >= instead of > would give both a vote here and evict a
      // different position two steps later, which is a much harder failure to
      // read than this one.
      params: { budget: 3, recentWindow: 1 },
      seed: 1,
      steps: [
        { call: "onDecodeStep", args: [1, [1.0]] },
        { call: "onDecodeStep", args: [2, [0.5, 0.5]] },
        { call: "votesOf", args: [0], expect: 0 },
        { call: "votesOf", args: [1], expect: 0 },
        { call: "onDecodeStep", args: [3, [0.25, 0.25, 0.5]] },
        { call: "votesOf", args: [2], expect: 1 },
        // Positions 0 and 1 are both on zero; the lower one goes.
        { call: "evict", args: [3], expect: [0] },
      ],
    },
    {
      name: "boundary: the recent window outranks the votes",
      // Position 2 has never voted and position 1 has never voted either, but
      // position 2 is inside the two-position recent window. The protection
      // decides it.
      params: { budget: 3, recentWindow: 2 },
      seed: 1,
      steps: [
        { call: "onDecodeStep", args: [1, [1.0]] },
        { call: "onDecodeStep", args: [2, [0.9375, 0.0625]] },
        { call: "onDecodeStep", args: [3, [0.5, 0.25, 0.25]] },
        { call: "votesOf", args: [0], expect: 2 },
        { call: "votesOf", args: [1], expect: 0 },
        { call: "votesOf", args: [2], expect: 0 },
        { call: "evict", args: [3], expect: [1] },
        { call: "votesOf", args: [2], expect: 0 },
      ],
    },
    {
      name: "distinguishing: an early spike buys one vote and no more",
      // The same steps as h2o's distinguishing case, budget and window and all.
      // Position 0 takes almost the whole first two steps and then nearly
      // nothing: one vote. Positions 1 and 2 clear their share on three steps
      // each, quietly. H2O keeps position 0 on a cumulative score of 2.0625 —
      // the highest of any position — and evicts position 3. This policy
      // evicts position 0.
      params: { budget: 5, recentWindow: 2 },
      seed: 1,
      steps: [
        { call: "onDecodeStep", args: [1, [1.0]] },
        { call: "onDecodeStep", args: [2, [0.875, 0.125]] },
        { call: "onDecodeStep", args: [3, [0.0625, 0.5, 0.4375]] },
        { call: "onDecodeStep", args: [4, [0.0625, 0.3125, 0.3125, 0.3125]] },
        { call: "onDecodeStep", args: [5, [0.0625, 0.25, 0.25, 0.25, 0.1875]] },
        // One vote, from step 2, and nothing since.
        { call: "votesOf", args: [0], expect: 1 },
        { call: "votesOf", args: [1], expect: 3 },
        { call: "votesOf", args: [2], expect: 3 },
        { call: "votesOf", args: [3], expect: 2 },
        // Position 4 arrived at step 4 and has cleared its share on no step:
        // 0.1875 is below the 0.2 share at step 5. It is protected anyway.
        { call: "votesOf", args: [4], expect: 0 },
        { call: "evict", args: [5], expect: [0] },
        { call: "votesOf", args: [0], expect: -1 },
      ],
    },
    {
      name: "tiebreak: equal votes evict the lower position",
      // Positions 1 and 2 are both on zero votes, so the earlier one goes.
      params: { budget: 3, recentWindow: 1 },
      seed: 1,
      steps: [
        { call: "onDecodeStep", args: [1, [1.0]] },
        { call: "onDecodeStep", args: [2, [0.875, 0.125]] },
        { call: "onDecodeStep", args: [3, [0.5, 0.25, 0.25]] },
        { call: "votesOf", args: [0], expect: 2 },
        { call: "votesOf", args: [1], expect: 0 },
        { call: "votesOf", args: [2], expect: 0 },
        { call: "evict", args: [3], expect: [1] },
      ],
    },
    {
      name: "several victims come back in position order, not in the order chosen",
      params: { budget: 5, recentWindow: 1 },
      seed: 1,
      steps: [
        { call: "onDecodeStep", args: [1, [1.0]] },
        { call: "onDecodeStep", args: [2, [0.75, 0.25]] },
        { call: "onDecodeStep", args: [3, [0.5, 0.25, 0.25]] },
        // Share is 1/4 here, and position 2's 0.25 does not exceed it.
        { call: "onDecodeStep", args: [4, [0.5, 0.125, 0.25, 0.125]] },
        { call: "votesOf", args: [0], expect: 3 },
        { call: "votesOf", args: [1], expect: 0 },
        { call: "votesOf", args: [2], expect: 0 },
        { call: "votesOf", args: [3], expect: 0 },
        { call: "evict", args: [2], expect: [1, 2, 3] },
        { call: "keptCount", args: [], expect: 2 },
      ],
    },
    {
      name: "a step with no attention leaves every vote untouched",
      params: { budget: 3, recentWindow: 1 },
      seed: 1,
      steps: [
        { call: "onDecodeStep", args: [1, [1.0]] },
        { call: "onDecodeStep", args: [2, [0.75, 0.25]] },
        { call: "votesOf", args: [0], expect: 1 },
        { call: "onDecodeStep", args: [3, null] },
        { call: "votesOf", args: [0], expect: 1 },
        { call: "votesOf", args: [1], expect: 0 },
        { call: "evict", args: [3], expect: [1] },
      ],
    },
  ],
};

export default scenarios;
