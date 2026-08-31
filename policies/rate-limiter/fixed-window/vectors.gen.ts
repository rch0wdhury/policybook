/**
 * Scenario script for rate-limiter/fixed-window.
 *
 * Time is an integer number of milliseconds and keys are integers (§2.1).
 * `allow` takes (key, cost, now); `retryAfter` takes (key, now).
 *
 * Regenerate with: pnpm gen:vectors rate-limiter/fixed-window
 */

import type { ScenarioFile } from "../../../packages/vectors/src/gen";

/** Small enough to reason about by hand: five permits a second. */
const SMALL = { limit: 5, windowMs: 1_000 };

const scenarios: ScenarioFile = {
  policy: "rate-limiter/fixed-window",
  cases: [
    {
      name: "smoke: the first `limit` requests pass and the next is refused",
      params: SMALL,
      seed: 1,
      steps: [
        { call: "allow", args: [1, 1, 0], expect: true },
        { call: "allow", args: [1, 1, 10], expect: true },
        { call: "allow", args: [1, 1, 20], expect: true },
        { call: "allow", args: [1, 1, 30], expect: true },
        { call: "allow", args: [1, 1, 40], expect: true },
        { call: "countOf", args: [1, 40], expect: 5 },
        { call: "allow", args: [1, 1, 50], expect: false },
        // The window runs to 1,000, so the wait is what is left of it.
        { call: "retryAfter", args: [1, 50], expect: 950 },
        { call: "allow", args: [1, 1, 1_000], expect: true },
      ],
    },
    {
      name: "boundary: windows are aligned to the epoch, not to the first request",
      params: SMALL,
      seed: 1,
      steps: [
        // A first request at 900 sits in the window [0, 1000), so the reset
        // comes 100 ms later — not 1,000 ms after the request.
        { call: "allow", args: [7, 1, 900], expect: true },
        { call: "countOf", args: [7, 900], expect: 1 },
        { call: "countOf", args: [7, 1_000], expect: 0 },
        { call: "allow", args: [7, 4, 950], expect: true },
        { call: "allow", args: [7, 1, 960], expect: false },
        { call: "retryAfter", args: [7, 960], expect: 40 },
        { call: "allow", args: [7, 5, 1_000], expect: true },
      ],
    },
    {
      name: "distinguishing: twice the limit passes across a window boundary",
      params: SMALL,
      seed: 1,
      steps: [
        // Five at 999 and five at 1,000: ten requests in two milliseconds
        // against a five-per-second limit. A sliding log refuses all five of
        // the second group and a sliding counter refuses them too; this is the
        // flaw that motivates both.
        { call: "allow", args: [3, 5, 999], expect: true },
        { call: "allow", args: [3, 1, 999], expect: false },
        { call: "allow", args: [3, 5, 1_000], expect: true },
        { call: "countOf", args: [3, 1_000], expect: 5 },
        { call: "allow", args: [3, 1, 1_000], expect: false },
      ],
    },
    {
      name: "tiebreak: a request costing exactly the remaining budget is admitted",
      params: SMALL,
      seed: 1,
      steps: [
        { call: "allow", args: [2, 3, 0], expect: true },
        // Two left. Three is refused, two is admitted — the comparison is
        // count + cost > limit, so equality passes.
        { call: "allow", args: [2, 3, 100], expect: false },
        { call: "allow", args: [2, 2, 100], expect: true },
        { call: "countOf", args: [2, 100], expect: 5 },
        { call: "allow", args: [2, 1, 100], expect: false },
        // A cost larger than the limit can never be admitted, even when empty.
        { call: "allow", args: [9, 6, 0], expect: false },
        { call: "countOf", args: [9, 0], expect: 0 },
      ],
    },
    {
      name: "keys are independent, and each is remembered",
      params: SMALL,
      seed: 1,
      steps: [
        { call: "allow", args: [1, 5, 0], expect: true },
        { call: "allow", args: [1, 1, 0], expect: false },
        // A different key has its own budget.
        { call: "allow", args: [2, 5, 0], expect: true },
        { call: "stateSize", expect: 2 },
        // An unknown key has nothing to wait for.
        { call: "retryAfter", args: [99, 0], expect: 0 },
        { call: "stateSize", expect: 2 },
      ],
    },
  ],
};

export default scenarios;
