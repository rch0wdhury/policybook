/**
 * Scenario script for kv-cache/streaming-llm.
 *
 * `onDecodeStep(pos, attn)` tells the policy a token was generated at `pos`;
 * `evict(budget)` asks it to name positions to drop. The cache starts holding
 * position 0, whose token exists before the first decode step — and which is a
 * sink whenever any are configured.
 *
 * Every expectation is hand-derived from the rule "pin positions below `sinks`,
 * then drop the oldest of the rest", which is simple enough to work out by
 * counting. That is what makes these an independent check rather than a
 * recording of the implementation.
 *
 * Regenerate with: pnpm gen:vectors kv-cache/streaming-llm
 */

import type { ScenarioFile } from "../../../packages/vectors/src/gen";

const scenarios: ScenarioFile = {
  policy: "kv-cache/streaming-llm",
  cases: [
    {
      name: "smoke: sinks are pinned and the window slides past them",
      params: { budget: 6, sinks: 2 },
      seed: 1,
      steps: [
        // Held: {0} as a sink. Steps 1..5 give sinks {0,1} and window {2,3,4,5}.
        { call: "onDecodeStep", args: [1, null] },
        { call: "onDecodeStep", args: [2, null] },
        { call: "onDecodeStep", args: [3, null] },
        { call: "onDecodeStep", args: [4, null] },
        { call: "onDecodeStep", args: [5, null] },
        { call: "keptCount", args: [], expect: 6 },
        { call: "sinkCount", args: [], expect: 2 },
        // Seven held. Position 0 is pinned, so the oldest evictable is 2.
        { call: "onDecodeStep", args: [6, null] },
        { call: "evict", args: [6], expect: [2] },
        { call: "onDecodeStep", args: [7, null] },
        { call: "evict", args: [6], expect: [3] },
        // The sinks are still there after four evictions have gone past them.
        { call: "sinkCount", args: [], expect: 2 },
      ],
    },
    {
      name: "boundary: one recency slot, the smallest coherent configuration",
      // sinks = budget - 1 leaves exactly one window position: the newest token
      // and nothing else. Any tighter and the policy could not keep the token
      // it was just told about, which the constructor refuses.
      params: { budget: 3, sinks: 2 },
      seed: 1,
      steps: [
        { call: "onDecodeStep", args: [1, null] },
        { call: "onDecodeStep", args: [2, null] },
        { call: "keptCount", args: [], expect: 3 },
        { call: "onDecodeStep", args: [3, null] },
        // Sinks {0,1} plus window {2,3}: one too many, and 2 is the victim.
        { call: "evict", args: [3], expect: [2] },
        { call: "keptCount", args: [], expect: 3 },
      ],
    },
    {
      name: "distinguishing: the sinks survive where a sliding window drops them",
      // The same steps as sliding-window's distinguishing case, budget and all.
      // That policy evicts positions 0, 1 and 2 in turn, purely because they
      // are oldest. This one pins them and takes position 4 instead — the whole
      // of Xiao et al.'s contribution, in one comparison.
      params: { budget: 6, sinks: 4 },
      seed: 1,
      steps: [
        { call: "onDecodeStep", args: [1, null] },
        { call: "onDecodeStep", args: [2, null] },
        { call: "onDecodeStep", args: [3, null] },
        { call: "onDecodeStep", args: [4, null] },
        { call: "onDecodeStep", args: [5, null] },
        { call: "keptCount", args: [], expect: 6 },
        { call: "onDecodeStep", args: [6, null] },
        // Sinks {0,1,2,3} and window {4,5,6}: seven held, and the victim is 4.
        { call: "evict", args: [6], expect: [4] },
        { call: "onDecodeStep", args: [7, null] },
        { call: "evict", args: [6], expect: [5] },
        { call: "onDecodeStep", args: [8, null] },
        { call: "evict", args: [6], expect: [6] },
        // Four sinks, three evictions, and every sink still held.
        { call: "sinkCount", args: [], expect: 4 },
        { call: "keptCount", args: [], expect: 6 },
      ],
    },
    {
      name: "tiebreak: several victims come back oldest first",
      params: { budget: 6, sinks: 2 },
      seed: 1,
      steps: [
        { call: "onDecodeStep", args: [1, null] },
        { call: "onDecodeStep", args: [2, null] },
        { call: "onDecodeStep", args: [3, null] },
        { call: "onDecodeStep", args: [4, null] },
        { call: "onDecodeStep", args: [5, null] },
        // Sinks {0,1}, window {2,3,4,5}. Squeezing to three leaves one window
        // slot, so 2, 3 and 4 go in that order and the sinks are untouched.
        { call: "evict", args: [3], expect: [2, 3, 4] },
        { call: "sinkCount", args: [], expect: 2 },
        { call: "keptCount", args: [], expect: 3 },
      ],
    },
    {
      name: "sinks: zero makes it a plain sliding window",
      // Worth pinning because it is the honest description of what the sink
      // count buys. With none configured this policy must agree with
      // sliding-window step for step, including evicting position 0 first.
      params: { budget: 4, sinks: 0 },
      seed: 1,
      steps: [
        { call: "onDecodeStep", args: [1, null] },
        { call: "onDecodeStep", args: [2, null] },
        { call: "onDecodeStep", args: [3, null] },
        { call: "sinkCount", args: [], expect: 0 },
        { call: "onDecodeStep", args: [4, null] },
        { call: "evict", args: [4], expect: [0] },
        { call: "onDecodeStep", args: [5, null] },
        { call: "evict", args: [4], expect: [1] },
      ],
    },
    {
      name: "the attention is ignored, and passing it changes nothing",
      // This policy finds structurally special positions, not important ones.
      // Position 5 carries almost all the attention here and is evicted anyway,
      // which is precisely the limitation h2o exists to address.
      params: { budget: 6, sinks: 2 },
      seed: 1,
      steps: [
        { call: "onDecodeStep", args: [1, [1.0]] },
        { call: "onDecodeStep", args: [2, [0.5, 0.5]] },
        { call: "onDecodeStep", args: [3, [0.25, 0.25, 0.5]] },
        { call: "onDecodeStep", args: [4, [0.125, 0.125, 0.25, 0.5]] },
        { call: "onDecodeStep", args: [5, [0.125, 0.125, 0.125, 0.125, 0.5]] },
        { call: "onDecodeStep", args: [6, [0.0625, 0.0625, 0.0625, 0.0625, 0.75, 0.0625]] },
        // Position 2 goes because it is the oldest non-sink, not because of any
        // weight; position 4, which carried 0.75, stays for now.
        { call: "evict", args: [6], expect: [2] },
      ],
    },
  ],
};

export default scenarios;
