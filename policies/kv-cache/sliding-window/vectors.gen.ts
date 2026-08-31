/**
 * Scenario script for kv-cache/sliding-window.
 *
 * `onDecodeStep(pos, attn)` tells the policy a token was generated at `pos`;
 * `evict(budget)` asks it to name positions to drop and returns them in the
 * order it chose. The cache starts holding position 0, whose token exists
 * before the first decode step, so a step-`t` call finds `t` positions held.
 *
 * Every expectation here is hand-derived: this policy is simple enough that
 * "the oldest ones" can be worked out by counting, which is what makes these
 * vectors an independent check rather than a recording of the implementation.
 *
 * Regenerate with: pnpm gen:vectors kv-cache/sliding-window
 */

import type { ScenarioFile } from "../../../packages/vectors/src/gen";

/** Attention over four kept positions. Ignored, and present to prove it. */
const ATTN_4 = [0.5, 0.25, 0.125, 0.125];

const scenarios: ScenarioFile = {
  policy: "kv-cache/sliding-window",
  cases: [
    {
      name: "smoke: the oldest position goes first",
      params: { budget: 4 },
      seed: 1,
      steps: [
        // Held: {0}. Steps 1..3 fill it to {0,1,2,3} without exceeding four.
        { call: "onDecodeStep", args: [1, null] },
        { call: "onDecodeStep", args: [2, null] },
        { call: "onDecodeStep", args: [3, null] },
        { call: "keptCount", args: [], expect: 4 },
        // Step 4 makes five held, so one must go: position 0, the oldest.
        { call: "onDecodeStep", args: [4, null] },
        { call: "evict", args: [4], expect: [0] },
        { call: "keptCount", args: [], expect: 4 },
        // And again, in order.
        { call: "onDecodeStep", args: [5, null] },
        { call: "evict", args: [4], expect: [1] },
      ],
    },
    {
      name: "boundary: a budget of one keeps only the newest token",
      params: { budget: 1 },
      seed: 1,
      steps: [
        // The degenerate case, and it should still be coherent: every step
        // evicts the position before it.
        { call: "onDecodeStep", args: [1, null] },
        { call: "evict", args: [1], expect: [0] },
        { call: "onDecodeStep", args: [2, null] },
        { call: "evict", args: [1], expect: [1] },
        { call: "keptCount", args: [], expect: 1 },
      ],
    },
    {
      name: "distinguishing: the attention sinks are dropped first, not last",
      // The entire difference from streaming-llm, and the reason that policy
      // exists. Positions 0-3 are the sinks a transformer keeps returning to;
      // this policy, knowing nothing about them, evicts them before anything
      // else purely because they are oldest. Run the same steps against
      // streaming-llm with sinks=4 and it drops position 4 instead.
      params: { budget: 6 },
      seed: 1,
      steps: [
        { call: "onDecodeStep", args: [1, null] },
        { call: "onDecodeStep", args: [2, null] },
        { call: "onDecodeStep", args: [3, null] },
        { call: "onDecodeStep", args: [4, null] },
        { call: "onDecodeStep", args: [5, null] },
        // Held: {0,1,2,3,4,5} — exactly the budget, nothing to do yet.
        { call: "keptCount", args: [], expect: 6 },
        { call: "onDecodeStep", args: [6, null] },
        // Seven held. The victim is position 0: the first token of the
        // sequence, and the one a real model would miss most.
        { call: "evict", args: [6], expect: [0] },
        { call: "onDecodeStep", args: [7, null] },
        { call: "evict", args: [6], expect: [1] },
        { call: "onDecodeStep", args: [8, null] },
        { call: "evict", args: [6], expect: [2] },
      ],
    },
    {
      name: "tiebreak: eviction order is arrival order, oldest first",
      // When several positions must go at once they come back oldest-first,
      // which is the domain's tie-break rule (lower position first) and here
      // coincides with arrival order because positions only ever increase.
      params: { budget: 5 },
      seed: 1,
      steps: [
        { call: "onDecodeStep", args: [1, null] },
        { call: "onDecodeStep", args: [2, null] },
        { call: "onDecodeStep", args: [3, null] },
        { call: "onDecodeStep", args: [4, null] },
        { call: "onDecodeStep", args: [5, null] },
        // Six held against a budget of five. Asking for a budget of two forces
        // four out in one call, which is where the ordering becomes visible.
        { call: "evict", args: [2], expect: [0, 1, 2, 3] },
        { call: "keptCount", args: [], expect: 2 },
      ],
    },
    {
      name: "the attention is ignored, and passing it changes nothing",
      // A policy that quietly read the weights would be a different policy.
      // These steps are the smoke case with attention supplied, and the
      // expectations are identical.
      params: { budget: 4 },
      seed: 1,
      steps: [
        { call: "onDecodeStep", args: [1, [1.0]] },
        { call: "onDecodeStep", args: [2, [0.75, 0.25]] },
        { call: "onDecodeStep", args: [3, [0.5, 0.25, 0.25]] },
        { call: "onDecodeStep", args: [4, ATTN_4] },
        // Position 0 carries half the attention in ATTN_4 and goes anyway.
        { call: "evict", args: [4], expect: [0] },
      ],
    },
  ],
};

export default scenarios;
