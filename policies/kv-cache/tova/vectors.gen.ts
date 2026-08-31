/**
 * Scenario script for kv-cache/tova.
 *
 * `onDecodeStep(pos, attn)` overwrites each kept position's record with this
 * step's attention — no accumulation — and admits `pos` as unobserved.
 * `evict(budget)` drops the lowest recorded weights. The cache starts holding
 * position 0, whose token exists before the first decode step.
 *
 * Every attention value is a binary fraction, so the expectations are read
 * straight off the last step's vector rather than captured. `lastAttentionOf`
 * pins that the record is *replaced* rather than added to, which is the one
 * thing separating this policy from h2o.
 *
 * Regenerate with: pnpm gen:vectors kv-cache/tova
 */

import type { ScenarioFile } from "../../../packages/vectors/src/gen";

const scenarios: ScenarioFile = {
  policy: "kv-cache/tova",
  cases: [
    {
      name: "smoke: the lowest weight on the latest step goes",
      params: { budget: 3 },
      seed: 1,
      steps: [
        { call: "onDecodeStep", args: [1, [1.0]] },
        { call: "onDecodeStep", args: [2, [0.25, 0.75]] },
        { call: "onDecodeStep", args: [3, [0.5, 0.25, 0.25]] },
        // Records are exactly the latest vector, not a running total.
        { call: "lastAttentionOf", args: [0], expect: 0.5 },
        { call: "lastAttentionOf", args: [1], expect: 0.25 },
        { call: "lastAttentionOf", args: [2], expect: 0.25 },
        // Positions 1 and 2 tie at 0.25, so the lower one goes.
        { call: "evict", args: [3], expect: [1] },
        { call: "keptCount", args: [], expect: 3 },
      ],
    },
    {
      name: "the record is replaced, not accumulated",
      // Position 0 collects 1.0 then 0.875 then 0.0625. Under h2o that is a
      // cumulative 1.9375 and the highest score in the cache; here it is just
      // 0.0625, because the earlier steps left nothing behind.
      params: { budget: 3 },
      seed: 1,
      steps: [
        { call: "onDecodeStep", args: [1, [1.0]] },
        { call: "lastAttentionOf", args: [0], expect: 1.0 },
        { call: "onDecodeStep", args: [2, [0.875, 0.125]] },
        { call: "lastAttentionOf", args: [0], expect: 0.875 },
        { call: "onDecodeStep", args: [3, [0.0625, 0.5, 0.4375]] },
        { call: "lastAttentionOf", args: [0], expect: 0.0625 },
        { call: "evict", args: [3], expect: [0] },
      ],
    },
    {
      name: "boundary: the token just generated is never the victim",
      // It has not appeared in any attention vector yet, so there is no weight
      // to rank it on. Scoring its absence as zero would evict every token on
      // the step it was created and the cache would never advance.
      params: { budget: 2 },
      seed: 1,
      steps: [
        { call: "onDecodeStep", args: [1, [1.0]] },
        { call: "onDecodeStep", args: [2, [0.5, 0.5]] },
        // Held: 0 and 1 tied at 0.5, and 2 with no record at all. The victim is
        // position 0, not the position with nothing.
        { call: "lastAttentionOf", args: [2], expect: -1 },
        { call: "evict", args: [2], expect: [0] },
        { call: "keptCount", args: [], expect: 2 },
      ],
    },
    {
      name: "distinguishing: the highest cumulative attention is evicted anyway",
      // The same steps as h2o's counterpart case. Position 0 takes almost all
      // of the first three steps — a cumulative 2.6875, far the highest — and
      // then goes quiet. H2O keeps it and evicts position 1; this policy sees
      // only the latest step, where position 0 is tied for lowest, and evicts
      // position 0. Nothing a position did earlier defends it here.
      params: { budget: 4 },
      seed: 1,
      steps: [
        { call: "onDecodeStep", args: [1, [1.0]] },
        { call: "onDecodeStep", args: [2, [0.875, 0.125]] },
        { call: "onDecodeStep", args: [3, [0.75, 0.125, 0.125]] },
        { call: "onDecodeStep", args: [4, [0.0625, 0.0625, 0.375, 0.5]] },
        { call: "lastAttentionOf", args: [0], expect: 0.0625 },
        { call: "lastAttentionOf", args: [1], expect: 0.0625 },
        { call: "lastAttentionOf", args: [2], expect: 0.375 },
        { call: "lastAttentionOf", args: [3], expect: 0.5 },
        // Tied at the bottom with position 1; the lower position goes.
        { call: "evict", args: [4], expect: [0] },
      ],
    },
    {
      name: "tiebreak: equal weights evict the lower position",
      params: { budget: 3 },
      seed: 1,
      steps: [
        { call: "onDecodeStep", args: [1, [1.0]] },
        { call: "onDecodeStep", args: [2, [0.5, 0.5]] },
        { call: "onDecodeStep", args: [3, [0.5, 0.25, 0.25]] },
        { call: "evict", args: [3], expect: [1] },
      ],
    },
    {
      name: "several victims come back in position order, not in the order chosen",
      params: { budget: 4 },
      seed: 1,
      steps: [
        { call: "onDecodeStep", args: [1, [1.0]] },
        { call: "onDecodeStep", args: [2, [0.5, 0.5]] },
        { call: "onDecodeStep", args: [3, [0.25, 0.25, 0.5]] },
        { call: "onDecodeStep", args: [4, [0.5, 0.125, 0.25, 0.125]] },
        // Records: 0 -> 0.5, 1 -> 0.125, 2 -> 0.25, 3 -> 0.125, 4 -> none.
        // Squeezing to two picks 1, then 3, then 2 — returned ascending.
        { call: "evict", args: [2], expect: [1, 2, 3] },
        { call: "keptCount", args: [], expect: 2 },
      ],
    },
    {
      name: "a step with no attention leaves every record untouched",
      params: { budget: 3 },
      seed: 1,
      steps: [
        { call: "onDecodeStep", args: [1, [1.0]] },
        { call: "onDecodeStep", args: [2, [0.75, 0.25]] },
        { call: "onDecodeStep", args: [3, null] },
        { call: "lastAttentionOf", args: [0], expect: 0.75 },
        { call: "lastAttentionOf", args: [1], expect: 0.25 },
        { call: "evict", args: [3], expect: [1] },
      ],
    },
  ],
};

export default scenarios;
