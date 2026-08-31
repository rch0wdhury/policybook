/**
 * Scenario script for rate-limiter/dual-bucket.
 *
 * Time is an integer number of milliseconds and keys are integers (§2.1).
 * `allow` takes (key, cost, now) where `cost` is the work the call asks for;
 * the call always charges one request. `retryAfter`, `requestsOf` and
 * `tokensOf` take (key, now).
 *
 * The numbers are deliberately small — three requests and a thousand units per
 * minute — so every step can be checked by hand. Real configurations look like
 * the defaults: 500 and 200,000.
 *
 * Regenerate with: pnpm gen:vectors rate-limiter/dual-bucket
 */

import type { ScenarioFile } from "../../../packages/vectors/src/gen";

/** Three calls and a thousand units a minute. */
const SMALL = { requestsPerMin: 3, tokensPerMin: 1_000 };

const scenarios: ScenarioFile = {
  policy: "rate-limiter/dual-bucket",
  cases: [
    {
      name: "smoke: every call charges both dimensions",
      params: SMALL,
      seed: 1,
      steps: [
        { call: "requestsOf", args: [1, 0], expect: 3 },
        { call: "tokensOf", args: [1, 0], expect: 1_000 },
        { call: "allow", args: [1, 100, 0], expect: true },
        // One request and a hundred units, from a single call.
        { call: "requestsOf", args: [1, 0], expect: 2 },
        { call: "tokensOf", args: [1, 0], expect: 900 },
        { call: "allow", args: [1, 100, 0], expect: true },
        { call: "allow", args: [1, 100, 0], expect: true },
        { call: "requestsOf", args: [1, 0], expect: 0 },
        { call: "tokensOf", args: [1, 0], expect: 700 },
      ],
    },
    {
      name: "distinguishing: either dimension refuses on its own",
      params: SMALL,
      seed: 1,
      steps: [
        // Three small calls exhaust the request ceiling while seven hundred
        // units are still unspent. This is the client stuck in a retry loop,
        // and a token-only limiter would not see it.
        { call: "allow", args: [1, 100, 0], expect: true },
        { call: "allow", args: [1, 100, 0], expect: true },
        { call: "allow", args: [1, 100, 0], expect: true },
        { call: "allow", args: [1, 100, 0], expect: false },
        { call: "requestsOf", args: [1, 0], expect: 0 },
        { call: "tokensOf", args: [1, 0], expect: 700 },
        // Three calls a minute is one every twenty seconds.
        { call: "retryAfter", args: [1, 0], expect: 20_000 },

        // The opposite failure on a fresh key: one enormous call exhausts the
        // work ceiling while two of three requests remain. This is the caller
        // submitting a book, and a request-only limiter would not see it.
        { call: "allow", args: [2, 1_000, 0], expect: true },
        { call: "requestsOf", args: [2, 0], expect: 2 },
        { call: "tokensOf", args: [2, 0], expect: 0 },
        { call: "allow", args: [2, 1, 0], expect: false },
        // A thousand units a minute is one every sixty milliseconds, so this
        // key waits far less than the other — the limit that bit is the limit
        // that governs the wait.
        { call: "retryAfter", args: [2, 0], expect: 60 },
        { call: "tokensOf", args: [2, 60], expect: 1 },
        { call: "allow", args: [2, 1, 60], expect: true },
      ],
    },
    {
      name: "tiebreak: a refused call charges nothing, on either dimension",
      params: SMALL,
      seed: 1,
      steps: [
        { call: "allow", args: [2, 1_000, 0], expect: true },
        { call: "requestsOf", args: [2, 0], expect: 2 },
        // Refused for work. The request dimension must be untouched: a caller
        // that retries would otherwise be throttled harder for retrying, which
        // is the bug this atomicity exists to prevent.
        { call: "allow", args: [2, 1, 0], expect: false },
        { call: "requestsOf", args: [2, 0], expect: 2 },
        { call: "tokensOf", args: [2, 0], expect: 0 },

        // A call costing exactly what is left is admitted: the comparison is
        // tokens < cost, so equality passes.
        { call: "allow", args: [6, 600, 0], expect: true },
        { call: "tokensOf", args: [6, 0], expect: 400 },
        { call: "allow", args: [6, 500, 0], expect: false },
        { call: "allow", args: [6, 400, 0], expect: true },
        { call: "tokensOf", args: [6, 0], expect: 0 },

        // A cost above the whole minute's allowance can never be met, and
        // still charges nothing.
        { call: "allow", args: [7, 1_001, 0], expect: false },
        { call: "requestsOf", args: [7, 0], expect: 3 },
        { call: "tokensOf", args: [7, 0], expect: 1_000 },
      ],
    },
    {
      name: "boundary: both ceilings saturate, and idling past a minute adds nothing",
      params: SMALL,
      seed: 1,
      steps: [
        { call: "allow", args: [3, 1_000, 0], expect: true },
        { call: "requestsOf", args: [3, 0], expect: 2 },
        { call: "tokensOf", args: [3, 0], expect: 0 },
        // A minute refills both dimensions completely, and each stops at its
        // own ceiling rather than continuing to accrue.
        { call: "requestsOf", args: [3, 60_000], expect: 3 },
        { call: "tokensOf", args: [3, 60_000], expect: 1_000 },
        // A month is worth exactly one minute: elapsed time is clamped, which
        // is what keeps the multiply bounded in the C port.
        { call: "requestsOf", args: [3, 2_592_000_000], expect: 3 },
        { call: "tokensOf", args: [3, 2_592_000_000], expect: 1_000 },
        { call: "retryAfter", args: [3, 2_592_000_000], expect: 0 },
        { call: "stateSize", expect: 1 },
      ],
    },
    {
      name: "the two dimensions refill independently",
      params: SMALL,
      seed: 1,
      steps: [
        // Spend everything: three calls of 333, 333 and 334 units.
        { call: "allow", args: [4, 333, 0], expect: true },
        { call: "allow", args: [4, 333, 0], expect: true },
        { call: "allow", args: [4, 334, 0], expect: true },
        { call: "requestsOf", args: [4, 0], expect: 0 },
        { call: "tokensOf", args: [4, 0], expect: 0 },
        // Ten seconds later the work ceiling has recovered a sixth of its
        // minute while the request ceiling has not yet earned a whole call.
        { call: "tokensOf", args: [4, 10_000], expect: 166 },
        { call: "requestsOf", args: [4, 10_000], expect: 0 },
        { call: "allow", args: [4, 1, 10_000], expect: false },
        // The request dimension is the one holding it up, so its wait governs.
        { call: "retryAfter", args: [4, 10_000], expect: 10_000 },
        { call: "requestsOf", args: [4, 20_000], expect: 1 },
        { call: "allow", args: [4, 1, 20_000], expect: true },
      ],
    },
  ],
};

export default scenarios;
