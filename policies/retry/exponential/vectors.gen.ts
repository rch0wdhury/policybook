/**
 * Scenario script for retry/exponential.
 *
 * `nextDelay(attempt, error)` returns the milliseconds to wait, or null to give
 * up. `attempt` is 1-based and is the number of the attempt that just failed.
 *
 * Every delay here is hand-derived: `min(cap, base * 2^(attempt - 1))`. That is
 * the point of an un-jittered policy — its sequence is a pure function of the
 * attempt number, so it can be written out in full.
 *
 * Regenerate with: pnpm gen:vectors retry/exponential
 */

import type { ScenarioFile } from "../../../packages/vectors/src/gen";

const RETRYABLE = { status: 503, retryable: true };
/** The registry's reference configuration, with room to reach the cap. */
const REFERENCE = { baseMs: 100, capMs: 10_000, maxAttempts: 12 };

const scenarios: ScenarioFile = {
  policy: "retry/exponential",
  cases: [
    {
      name: "smoke: the delay doubles after every failure",
      params: REFERENCE,
      seed: 1,
      steps: [
        { call: "nextDelay", args: [1, RETRYABLE], expect: 100 },
        { call: "nextDelay", args: [2, RETRYABLE], expect: 200 },
        { call: "nextDelay", args: [3, RETRYABLE], expect: 400 },
        { call: "nextDelay", args: [4, RETRYABLE], expect: 800 },
        { call: "nextDelay", args: [5, RETRYABLE], expect: 1_600 },
        { call: "nextDelay", args: [6, RETRYABLE], expect: 3_200 },
        { call: "nextDelay", args: [7, RETRYABLE], expect: 6_400 },
      ],
    },
    {
      name: "boundary: the cap holds, and holds forever after",
      params: REFERENCE,
      seed: 1,
      steps: [
        // 100 x 2^7 is 12,800, which is over the ten-second cap.
        { call: "nextDelay", args: [8, RETRYABLE], expect: 10_000 },
        { call: "nextDelay", args: [9, RETRYABLE], expect: 10_000 },
        { call: "nextDelay", args: [11, RETRYABLE], expect: 10_000 },
        // Without the cap the twentieth retry would be a fortnight. The
        // doubling stops as soon as the cap is reached, so the arithmetic never
        // approaches the width of an integer however large `attempt` grows.
        { call: "nextDelay", args: [1_000_000, { status: 503, retryable: true }], expect: null },
      ],
    },
    {
      name: "distinguishing: the sequence is a pure function of the attempt number",
      // The difference from full jitter, and the reason not to ship this: two
      // clients that failed together get identical delays, so they retry
      // together, every time. Asking twice with the same attempt number gives
      // the same answer — there is no state and no draw anywhere.
      params: REFERENCE,
      seed: 1,
      steps: [
        { call: "nextDelay", args: [3, RETRYABLE], expect: 400 },
        { call: "nextDelay", args: [3, RETRYABLE], expect: 400 },
        { call: "nextDelay", args: [3, RETRYABLE], expect: 400 },
      ],
    },
    {
      name: "tiebreak: give-up at maxAttempts, and on a permanent failure",
      params: { baseMs: 100, capMs: 10_000, maxAttempts: 3 },
      seed: 1,
      steps: [
        { call: "nextDelay", args: [1, RETRYABLE], expect: 100 },
        { call: "nextDelay", args: [2, RETRYABLE], expect: 200 },
        { call: "nextDelay", args: [3, RETRYABLE], expect: null },
        { call: "nextDelay", args: [1, { status: 400, retryable: false }], expect: null },
      ],
    },
    {
      name: "a cap below the base clamps the very first delay",
      // Not a configuration anyone should write, but it should behave rather
      // than surprise: the cap wins from attempt one.
      params: { baseMs: 5_000, capMs: 1_000, maxAttempts: 4 },
      seed: 1,
      steps: [
        { call: "nextDelay", args: [1, RETRYABLE], expect: 1_000 },
        { call: "nextDelay", args: [2, RETRYABLE], expect: 1_000 },
      ],
    },
  ],
};

export default scenarios;
