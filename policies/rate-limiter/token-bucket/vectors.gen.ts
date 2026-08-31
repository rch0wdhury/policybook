/**
 * Scenario script for rate-limiter/token-bucket.
 *
 * Time is an integer number of milliseconds and keys are integers (§2.1).
 * `allow` takes (key, cost, now); `retryAfter` and `tokensOf` take (key, now).
 *
 * `tokensOf` refills as a side effect, exactly as `allow` does — it reports the
 * balance *at* the time it is asked.
 *
 * Regenerate with: pnpm gen:vectors rate-limiter/token-bucket
 */

import type { ScenarioFile } from "../../../packages/vectors/src/gen";

/** 100 tokens a second is one every 10 ms; a burst of five is easy to follow. */
const SMALL = { ratePerSec: 100, burst: 5 };

const scenarios: ScenarioFile = {
  policy: "rate-limiter/token-bucket",
  cases: [
    {
      name: "smoke: spend the burst, then one token every 10 ms",
      params: SMALL,
      seed: 1,
      steps: [
        // A key never seen starts full: it has been idle for all of history.
        { call: "tokensOf", args: [1, 0], expect: 5 },
        { call: "allow", args: [1, 1, 0], expect: true },
        { call: "allow", args: [1, 1, 0], expect: true },
        { call: "allow", args: [1, 1, 0], expect: true },
        { call: "allow", args: [1, 1, 0], expect: true },
        { call: "allow", args: [1, 1, 0], expect: true },
        { call: "tokensOf", args: [1, 0], expect: 0 },
        { call: "allow", args: [1, 1, 0], expect: false },
        // 100 per second is one per 10 ms, and the answer is exact rather than
        // "wait for the window to turn".
        { call: "retryAfter", args: [1, 0], expect: 10 },
        { call: "allow", args: [1, 1, 9], expect: false },
        { call: "allow", args: [1, 1, 10], expect: true },
        { call: "tokensOf", args: [1, 10], expect: 0 },
      ],
    },
    {
      name: "boundary: the bucket saturates at burst, however long it idles",
      params: SMALL,
      seed: 1,
      steps: [
        { call: "allow", args: [1, 5, 0], expect: true },
        { call: "tokensOf", args: [1, 0], expect: 0 },
        // Fifty milliseconds is exactly a full refill at this rate.
        { call: "tokensOf", args: [1, 50], expect: 5 },
        // A month of idling adds nothing further: the overflow is discarded,
        // which is what stops a quiet key banking an unbounded burst.
        { call: "tokensOf", args: [1, 2_592_000_000], expect: 5 },
        { call: "allow", args: [1, 5, 2_592_000_000], expect: true },
        { call: "allow", args: [1, 1, 2_592_000_000], expect: false },
      ],
    },
    {
      name: "distinguishing: the balance refills continuously, with no window edge",
      params: SMALL,
      seed: 1,
      steps: [
        { call: "allow", args: [2, 5, 0], expect: true },
        { call: "allow", args: [2, 1, 0], expect: false },
        // Ten milliseconds later exactly one token exists — not five, and not
        // zero-until-the-edge. A fixed window would still be refusing here and
        // would then return the whole budget at once; a sliding counter fades
        // its budget back but only relative to a window it aligns to. This has
        // no window at all.
        { call: "tokensOf", args: [2, 10], expect: 1 },
        { call: "tokensOf", args: [2, 25], expect: 2 },
        { call: "retryAfter", args: [2, 25], expect: 0 },
        { call: "tokensOf", args: [2, 50], expect: 5 },
      ],
    },
    {
      name: "tiebreak: a request costing exactly the balance is admitted",
      params: SMALL,
      seed: 1,
      steps: [
        { call: "allow", args: [3, 3, 0], expect: true },
        { call: "tokensOf", args: [3, 0], expect: 2 },
        { call: "allow", args: [3, 3, 0], expect: false },
        // Two left, and two is spendable: the comparison is tokens < cost, so
        // equality passes.
        { call: "allow", args: [3, 2, 0], expect: true },
        { call: "allow", args: [3, 1, 0], expect: false },
        // A cost above the burst can never be met, even from a full bucket.
        { call: "allow", args: [4, 6, 0], expect: false },
        { call: "tokensOf", args: [4, 0], expect: 5 },
      ],
    },
    {
      name: "the fractional carry survives between refills",
      // Three tokens a second divides no whole number of milliseconds, so the
      // remainder has to be carried rather than rounded away. This is the case
      // a floating-point balance would eventually get wrong.
      params: { ratePerSec: 3, burst: 2 },
      seed: 1,
      steps: [
        { call: "allow", args: [1, 2, 0], expect: true },
        { call: "tokensOf", args: [1, 0], expect: 0 },
        // 3 x 333 = 999 thousandths: not yet a token.
        { call: "tokensOf", args: [1, 333], expect: 0 },
        // One millisecond more carries the 999 across the boundary. An
        // implementation that recomputed from scratch each time, or rounded the
        // remainder away, would still be at zero here.
        { call: "tokensOf", args: [1, 334], expect: 1 },
        { call: "allow", args: [1, 1, 334], expect: true },
        { call: "allow", args: [1, 1, 334], expect: false },
        { call: "retryAfter", args: [1, 334], expect: 333 },
      ],
    },
    {
      name: "equivalence: the mirror of the leaky bucket's own case",
      // Substituting tokens = capacity - level turns this policy into the leaky
      // bucket line for line. The same steps appear in that policy's vectors
      // with `levelOf` in place of `tokensOf` and the complementary values, so
      // the two files can be read side by side; `bucket-policies.test.ts`
      // checks the equivalence over a random trace rather than one scenario.
      params: SMALL,
      seed: 1,
      steps: [
        { call: "tokensOf", args: [7, 0], expect: 5 },
        { call: "allow", args: [7, 2, 0], expect: true },
        { call: "tokensOf", args: [7, 0], expect: 3 },
        { call: "allow", args: [7, 3, 0], expect: true },
        { call: "tokensOf", args: [7, 0], expect: 0 },
        { call: "allow", args: [7, 1, 0], expect: false },
        { call: "retryAfter", args: [7, 0], expect: 10 },
        { call: "tokensOf", args: [7, 30], expect: 3 },
        { call: "allow", args: [7, 3, 30], expect: true },
        { call: "tokensOf", args: [7, 30], expect: 0 },
        { call: "stateSize", expect: 1 },
      ],
    },
  ],
};

export default scenarios;
