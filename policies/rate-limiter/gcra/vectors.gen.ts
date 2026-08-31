/**
 * Scenario script for rate-limiter/gcra.
 *
 * Time is an integer number of milliseconds and keys are integers (§2.1).
 * `allow` takes (key, cost, now); `retryAfter` and `tokensOf` take (key, now).
 *
 * Several cases are deliberate copies of `rate-limiter/token-bucket`'s, with
 * the same steps and the same expected values, because the two policies decide
 * identically. Reading them side by side is the clearest statement of that.
 *
 * Regenerate with: pnpm gen:vectors rate-limiter/gcra
 */

import type { ScenarioFile } from "../../../packages/vectors/src/gen";

/** 100 permits a second is one every 10 ms; a burst of five is easy to follow. */
const SMALL = { ratePerSec: 100, burst: 5 };

const scenarios: ScenarioFile = {
  policy: "rate-limiter/gcra",
  cases: [
    {
      name: "smoke: spend the burst, then one permit every 10 ms",
      params: SMALL,
      seed: 1,
      steps: [
        { call: "tokensOf", args: [1, 0], expect: 5 },
        { call: "allow", args: [1, 1, 0], expect: true },
        { call: "allow", args: [1, 1, 0], expect: true },
        { call: "allow", args: [1, 1, 0], expect: true },
        { call: "allow", args: [1, 1, 0], expect: true },
        { call: "allow", args: [1, 1, 0], expect: true },
        { call: "tokensOf", args: [1, 0], expect: 0 },
        { call: "allow", args: [1, 1, 0], expect: false },
        { call: "retryAfter", args: [1, 0], expect: 10 },
        { call: "allow", args: [1, 1, 9], expect: false },
        { call: "allow", args: [1, 1, 10], expect: true },
        { call: "tokensOf", args: [1, 10], expect: 0 },
      ],
    },
    {
      name: "boundary: the schedule restarts from now, so idling banks nothing extra",
      params: SMALL,
      seed: 1,
      steps: [
        { call: "allow", args: [1, 5, 0], expect: true },
        { call: "tokensOf", args: [1, 0], expect: 0 },
        // Fifty milliseconds is exactly a full burst back at this rate.
        { call: "tokensOf", args: [1, 50], expect: 5 },
        // A month of idling adds nothing further. In GCRA this is the `max` in
        // the update: the schedule restarts from now rather than from a TAT
        // left far in the past, which is what caps the burst.
        { call: "tokensOf", args: [1, 2_592_000_000], expect: 5 },
        { call: "allow", args: [1, 5, 2_592_000_000], expect: true },
        { call: "allow", args: [1, 1, 2_592_000_000], expect: false },
      ],
    },
    {
      name: "distinguishing: identical to the token bucket, step for step",
      // Not a difference but a sameness, and it is the useful thing to know
      // about this policy. Every step and every value below is copied from
      // `rate-limiter/token-bucket`'s distinguishing case. GCRA reaches them
      // from one integer per key where the token bucket needs three, and that
      // is the whole reason to choose it.
      params: SMALL,
      seed: 1,
      steps: [
        { call: "allow", args: [2, 5, 0], expect: true },
        { call: "allow", args: [2, 1, 0], expect: false },
        { call: "tokensOf", args: [2, 10], expect: 1 },
        { call: "tokensOf", args: [2, 25], expect: 2 },
        { call: "retryAfter", args: [2, 25], expect: 0 },
        { call: "tokensOf", args: [2, 50], expect: 5 },
        { call: "stateSize", expect: 1 },
      ],
    },
    {
      name: "tiebreak: a request costing exactly the available permits is admitted",
      params: SMALL,
      seed: 1,
      steps: [
        { call: "allow", args: [3, 3, 0], expect: true },
        { call: "tokensOf", args: [3, 0], expect: 2 },
        { call: "allow", args: [3, 3, 0], expect: false },
        { call: "allow", args: [3, 2, 0], expect: true },
        { call: "allow", args: [3, 1, 0], expect: false },
        // A cost above the burst can never be met. The conformance test alone
        // would admit it for a key whose TAT sits far in the past, so the
        // ceiling is checked explicitly.
        { call: "allow", args: [4, 6, 0], expect: false },
        { call: "tokensOf", args: [4, 0], expect: 5 },
      ],
    },
    {
      name: "the scaled arithmetic keeps a fraction the textbook form would round",
      // Three permits a second means an emission interval of 333.33 ms, which
      // no integer can hold. Scaling by the rate turns it into exactly 1,000
      // units per permit and 3 units per millisecond, so the boundary lands on
      // millisecond 334 — the same millisecond the token bucket's carry crosses.
      params: { ratePerSec: 3, burst: 2 },
      seed: 1,
      steps: [
        { call: "allow", args: [1, 2, 0], expect: true },
        { call: "tokensOf", args: [1, 0], expect: 0 },
        { call: "retryAfter", args: [1, 0], expect: 334 },
        { call: "tokensOf", args: [1, 333], expect: 0 },
        { call: "allow", args: [1, 1, 333], expect: false },
        { call: "tokensOf", args: [1, 334], expect: 1 },
        { call: "allow", args: [1, 1, 334], expect: true },
        { call: "allow", args: [1, 1, 334], expect: false },
        { call: "retryAfter", args: [1, 334], expect: 333 },
      ],
    },
  ],
};

export default scenarios;
