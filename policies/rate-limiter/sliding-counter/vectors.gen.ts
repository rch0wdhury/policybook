/**
 * Scenario script for rate-limiter/sliding-counter.
 *
 * Time is an integer number of milliseconds and keys are integers (§2.1).
 * `allow` takes (key, cost, now); `retryAfter` and `estimateOf` take (key, now).
 *
 * `estimateOf` rolls the counter forward as a side effect, exactly as `allow`
 * does — it reports the weighted estimate *at* the time it is asked.
 *
 * Regenerate with: pnpm gen:vectors rate-limiter/sliding-counter
 */

import type { ScenarioFile } from "../../../packages/vectors/src/gen";

/** Small enough to reason about by hand: five permits a second. */
const SMALL = { limit: 5, windowMs: 1_000 };

const scenarios: ScenarioFile = {
  policy: "rate-limiter/sliding-counter",
  cases: [
    {
      name: "smoke: the previous window's count decays instead of vanishing",
      params: SMALL,
      seed: 1,
      steps: [
        { call: "allow", args: [1, 1, 0], expect: true },
        { call: "allow", args: [1, 1, 10], expect: true },
        { call: "allow", args: [1, 1, 20], expect: true },
        { call: "allow", args: [1, 1, 30], expect: true },
        { call: "allow", args: [1, 1, 40], expect: true },
        { call: "estimateOf", args: [1, 40], expect: 5 },
        { call: "allow", args: [1, 1, 50], expect: false },
        // At the window edge the whole previous count is still carried, so the
        // edge itself brings no relief — the wait runs one millisecond past it.
        { call: "retryAfter", args: [1, 50], expect: 951 },
        { call: "allow", args: [1, 1, 1_000], expect: false },
        { call: "estimateOf", args: [1, 1_000], expect: 5 },
        { call: "allow", args: [1, 1, 1_001], expect: true },
      ],
    },
    {
      name: "boundary: a gap of two windows clears both counts",
      params: SMALL,
      seed: 1,
      steps: [
        { call: "allow", args: [7, 5, 900], expect: true },
        { call: "allow", args: [7, 1, 1_000], expect: false },
        // Halfway into the new window, half the old count has faded.
        { call: "estimateOf", args: [7, 1_500], expect: 2 },
        { call: "allow", args: [7, 1, 1_500], expect: true },
        // Nothing from before is still in the trailing window after a full
        // window of silence, so both counts reset rather than one shifting.
        { call: "estimateOf", args: [7, 4_000], expect: 0 },
        { call: "allow", args: [7, 5, 4_000], expect: true },
      ],
    },
    {
      name: "distinguishing: a boundary burst is refused, then released gradually",
      params: SMALL,
      seed: 1,
      steps: [
        // The same trace all three window policies are compared on. A fixed
        // window admits the second group in full; a sliding log refuses it
        // until 1,999, when all five expire together; this releases the budget
        // continuously, which is the behaviour that makes it the practical
        // choice of the three.
        { call: "allow", args: [3, 5, 999], expect: true },
        { call: "allow", args: [3, 5, 1_000], expect: false },
        { call: "allow", args: [3, 1, 1_000], expect: false },
        // Halfway through the new window, three of the five permits are back.
        { call: "estimateOf", args: [3, 1_500], expect: 2 },
        { call: "allow", args: [3, 3, 1_500], expect: true },
        { call: "allow", args: [3, 1, 1_500], expect: false },
      ],
    },
    {
      name: "tiebreak: the weighting floors, and the floor decides the request",
      params: SMALL,
      seed: 1,
      steps: [
        { call: "allow", args: [2, 5, 0], expect: true },
        // One millisecond into the next window the exact carried value is
        // 5 x 999/1000 = 4.995. Flooring makes it 4, which leaves room for
        // exactly one request; without the floor the estimate would be 4.995
        // and this request would be refused. The floor is therefore not a
        // rounding detail but part of the policy's definition, and it is what
        // keeps the three language ports deciding identically.
        { call: "estimateOf", args: [2, 1_001], expect: 4 },
        { call: "allow", args: [2, 1, 1_001], expect: true },
        { call: "estimateOf", args: [2, 1_001], expect: 5 },
        { call: "allow", args: [2, 1, 1_001], expect: false },
        { call: "retryAfter", args: [2, 1_001], expect: 200 },
        { call: "allow", args: [2, 1, 1_201], expect: true },
      ],
    },
    {
      name: "keys are independent, and each is remembered",
      params: SMALL,
      seed: 1,
      steps: [
        { call: "allow", args: [1, 5, 0], expect: true },
        { call: "allow", args: [1, 1, 0], expect: false },
        { call: "allow", args: [2, 5, 0], expect: true },
        { call: "stateSize", expect: 2 },
        { call: "retryAfter", args: [99, 0], expect: 0 },
        { call: "stateSize", expect: 2 },
      ],
    },
  ],
};

export default scenarios;
