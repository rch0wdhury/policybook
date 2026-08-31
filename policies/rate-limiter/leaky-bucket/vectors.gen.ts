/**
 * Scenario script for rate-limiter/leaky-bucket.
 *
 * Time is an integer number of milliseconds and keys are integers (§2.1).
 * `allow` takes (key, cost, now); `retryAfter` and `levelOf` take (key, now).
 *
 * `levelOf` drains as a side effect, exactly as `allow` does — it reports the
 * level *at* the time it is asked.
 *
 * Several cases here are deliberate mirrors of the token bucket's, with
 * `levelOf` in place of `tokensOf` and the complementary values, so the two
 * files can be read side by side.
 *
 * Regenerate with: pnpm gen:vectors rate-limiter/leaky-bucket
 */

import type { ScenarioFile } from "../../../packages/vectors/src/gen";

/** Drains 100 a second — one unit every 10 ms — with room for five at once. */
const SMALL = { ratePerSec: 100, capacity: 5 };

const scenarios: ScenarioFile = {
  policy: "rate-limiter/leaky-bucket",
  cases: [
    {
      name: "smoke: fill the bucket, then one unit of room every 10 ms",
      params: SMALL,
      seed: 1,
      steps: [
        // A key never seen starts empty: it has been draining for all of history.
        { call: "levelOf", args: [1, 0], expect: 0 },
        { call: "allow", args: [1, 1, 0], expect: true },
        { call: "allow", args: [1, 1, 0], expect: true },
        { call: "allow", args: [1, 1, 0], expect: true },
        { call: "allow", args: [1, 1, 0], expect: true },
        { call: "allow", args: [1, 1, 0], expect: true },
        { call: "levelOf", args: [1, 0], expect: 5 },
        { call: "allow", args: [1, 1, 0], expect: false },
        { call: "retryAfter", args: [1, 0], expect: 10 },
        { call: "allow", args: [1, 1, 9], expect: false },
        { call: "allow", args: [1, 1, 10], expect: true },
        { call: "levelOf", args: [1, 10], expect: 5 },
      ],
    },
    {
      name: "boundary: the bucket empties and stays empty, however long it idles",
      params: SMALL,
      seed: 1,
      steps: [
        { call: "allow", args: [1, 5, 0], expect: true },
        { call: "levelOf", args: [1, 0], expect: 5 },
        // Fifty milliseconds is exactly a full drain at this rate.
        { call: "levelOf", args: [1, 50], expect: 0 },
        // A month of idling drains no further — the level floors at zero, which
        // is the mirror of the token bucket saturating at its burst.
        { call: "levelOf", args: [1, 2_592_000_000], expect: 0 },
        { call: "allow", args: [1, 5, 2_592_000_000], expect: true },
        { call: "allow", args: [1, 1, 2_592_000_000], expect: false },
      ],
    },
    {
      name: "distinguishing: at its default capacity it smooths to exact spacing",
      // This is what a leaky bucket is chosen for, and why its default capacity
      // is 1 rather than the domain's reference burst of 100. At capacity 1 no
      // two requests are ever admitted together: the policy enforces one every
      // 1000/ratePerSec milliseconds, which is precisely what a token bucket
      // with burst 100 exists *not* to do.
      params: { ratePerSec: 100, capacity: 1 },
      seed: 1,
      steps: [
        { call: "allow", args: [1, 1, 0], expect: true },
        { call: "allow", args: [1, 1, 0], expect: false },
        { call: "retryAfter", args: [1, 0], expect: 10 },
        { call: "allow", args: [1, 1, 9], expect: false },
        { call: "allow", args: [1, 1, 10], expect: true },
        { call: "allow", args: [1, 1, 10], expect: false },
        { call: "allow", args: [1, 1, 19], expect: false },
        { call: "allow", args: [1, 1, 20], expect: true },
        // Idling earns nothing back beyond the single slot: there is no budget
        // to save up, which is the whole point.
        { call: "allow", args: [1, 2, 10_000], expect: false },
        { call: "allow", args: [1, 1, 10_000], expect: true },
      ],
    },
    {
      name: "tiebreak: a request filling exactly the remaining room is admitted",
      params: SMALL,
      seed: 1,
      steps: [
        { call: "allow", args: [3, 3, 0], expect: true },
        { call: "levelOf", args: [3, 0], expect: 3 },
        { call: "allow", args: [3, 3, 0], expect: false },
        // Two units of room, and two fits: the comparison is level + cost >
        // capacity, so equality passes.
        { call: "allow", args: [3, 2, 0], expect: true },
        { call: "allow", args: [3, 1, 0], expect: false },
        // A cost above the capacity can never fit, even in an empty bucket.
        { call: "allow", args: [4, 6, 0], expect: false },
        { call: "levelOf", args: [4, 0], expect: 0 },
      ],
    },
    {
      name: "the fractional carry survives between drains",
      // Three units a second divides no whole number of milliseconds, so the
      // remainder has to be carried rather than rounded away.
      params: { ratePerSec: 3, capacity: 2 },
      seed: 1,
      steps: [
        { call: "allow", args: [1, 2, 0], expect: true },
        { call: "levelOf", args: [1, 0], expect: 2 },
        // 3 x 333 = 999 thousandths: not yet a whole unit drained.
        { call: "levelOf", args: [1, 333], expect: 2 },
        // One millisecond more carries the 999 across the boundary.
        { call: "levelOf", args: [1, 334], expect: 1 },
        { call: "allow", args: [1, 1, 334], expect: true },
        { call: "allow", args: [1, 1, 334], expect: false },
        { call: "retryAfter", args: [1, 334], expect: 333 },
      ],
    },
    {
      name: "equivalence: the mirror of the token bucket's own case",
      // The same steps as `rate-limiter/token-bucket`'s equivalence case, with
      // every value substituted by level = capacity - tokens. Reading the two
      // side by side is the clearest statement that these are one algorithm;
      // `bucket-policies.test.ts` checks it over a random trace rather than a
      // single scenario.
      params: SMALL,
      seed: 1,
      steps: [
        { call: "levelOf", args: [7, 0], expect: 0 },
        { call: "allow", args: [7, 2, 0], expect: true },
        { call: "levelOf", args: [7, 0], expect: 2 },
        { call: "allow", args: [7, 3, 0], expect: true },
        { call: "levelOf", args: [7, 0], expect: 5 },
        { call: "allow", args: [7, 1, 0], expect: false },
        { call: "retryAfter", args: [7, 0], expect: 10 },
        { call: "levelOf", args: [7, 30], expect: 2 },
        { call: "allow", args: [7, 3, 30], expect: true },
        { call: "levelOf", args: [7, 30], expect: 5 },
        { call: "stateSize", expect: 1 },
      ],
    },
  ],
};

export default scenarios;
