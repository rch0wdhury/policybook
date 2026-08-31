/**
 * Scenario script for rate-limiter/sliding-log.
 *
 * Time is an integer number of milliseconds and keys are integers (§2.1).
 * `allow` takes (key, cost, now); `retryAfter` and `countOf` take (key, now).
 *
 * Note that `countOf` expires entries as a side effect, exactly as `allow`
 * does — it reports what the window holds *at* the time it is asked, which is
 * the only thing it could honestly report.
 *
 * Regenerate with: pnpm gen:vectors rate-limiter/sliding-log
 */

import type { ScenarioFile } from "../../../packages/vectors/src/gen";

/** Small enough to reason about by hand: five permits a second. */
const SMALL = { limit: 5, windowMs: 1_000 };

const scenarios: ScenarioFile = {
  policy: "rate-limiter/sliding-log",
  cases: [
    {
      name: "smoke: the window slides with the clock, not with a fixed edge",
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
        // Room appears when the oldest entry leaves, one entry at a time —
        // there is no moment where the whole budget returns at once.
        { call: "retryAfter", args: [1, 50], expect: 950 },
        { call: "allow", args: [1, 1, 1_000], expect: true },
        { call: "countOf", args: [1, 1_000], expect: 5 },
        { call: "allow", args: [1, 1, 1_005], expect: false },
        { call: "retryAfter", args: [1, 1_005], expect: 5 },
        { call: "allow", args: [1, 1, 1_010], expect: true },
      ],
    },
    {
      name: "boundary: an entry leaves the window exactly windowMs after it arrived",
      params: SMALL,
      seed: 1,
      steps: [
        { call: "allow", args: [4, 1, 0], expect: true },
        // The window is (now - windowMs, now]: at 999 the entry is still in it,
        // at 1,000 it has gone. That choice is what makes "at most `limit` in
        // any windowMs interval" true as stated rather than off by one.
        { call: "countOf", args: [4, 999], expect: 1 },
        { call: "countOf", args: [4, 1_000], expect: 0 },
        { call: "stateSize", expect: 1 },
      ],
    },
    {
      name: "distinguishing: a boundary burst is refused, unlike a fixed window",
      params: SMALL,
      seed: 1,
      steps: [
        // The same trace the fixed-window entry admits ten requests on. Here
        // nothing has left the window, so the second group is refused in full.
        { call: "allow", args: [3, 5, 999], expect: true },
        { call: "allow", args: [3, 1, 999], expect: false },
        { call: "allow", args: [3, 5, 1_000], expect: false },
        { call: "allow", args: [3, 1, 1_000], expect: false },
        { call: "countOf", args: [3, 1_000], expect: 5 },
        // The five at 999 leave together at 1,999, and only then is there room.
        { call: "retryAfter", args: [3, 1_000], expect: 999 },
        { call: "allow", args: [3, 5, 1_999], expect: true },
      ],
    },
    {
      name: "tiebreak: a request costing exactly the free space is admitted",
      params: SMALL,
      seed: 1,
      steps: [
        { call: "allow", args: [2, 3, 0], expect: true },
        { call: "allow", args: [2, 3, 100], expect: false },
        // A cost occupies that many slots, all stamped with the same instant,
        // so they age out together.
        { call: "allow", args: [2, 2, 100], expect: true },
        { call: "countOf", args: [2, 100], expect: 5 },
        { call: "allow", args: [2, 1, 100], expect: false },
        // At 1,000 the three from time 0 have gone and the two from 100 remain.
        { call: "countOf", args: [2, 1_000], expect: 2 },
        { call: "allow", args: [2, 3, 1_000], expect: true },
        { call: "allow", args: [2, 1, 1_000], expect: false },
      ],
    },
    {
      name: "the ring wraps: entries reused after a full cycle stay in order",
      params: SMALL,
      seed: 1,
      steps: [
        // Fill the ring, empty it, and fill it again. The second fill starts at
        // the slot after the first, so a ring whose head or count arithmetic
        // was wrong would report the wrong oldest entry here.
        { call: "allow", args: [8, 5, 0], expect: true },
        { call: "countOf", args: [8, 1_000], expect: 0 },
        { call: "allow", args: [8, 3, 1_000], expect: true },
        { call: "allow", args: [8, 2, 1_500], expect: true },
        { call: "allow", args: [8, 1, 1_500], expect: false },
        // The oldest live entry is now the one at 1,000.
        { call: "retryAfter", args: [8, 1_500], expect: 500 },
        { call: "countOf", args: [8, 2_000], expect: 2 },
        { call: "allow", args: [8, 3, 2_000], expect: true },
      ],
    },
  ],
};

export default scenarios;
