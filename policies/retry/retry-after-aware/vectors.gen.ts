/**
 * Scenario script for retry/retry-after-aware.
 *
 * Most of these are hand-authored, because the hinted path involves no
 * randomness at all: the policy returns what the server asked for, clamped, and
 * that is arithmetic rather than a draw. Only the fallback path — where no hint
 * arrived — is captured.
 *
 * Regenerate with: pnpm gen:vectors retry/retry-after-aware
 */

import type { ScenarioFile } from "../../../packages/vectors/src/gen";
import type { JsonValue } from "../../../packages/vectors/src/types";

const RETRYABLE = { status: 503, retryable: true };
const REFERENCE = { baseMs: 100, capMs: 10_000, maxAttempts: 12 };

/** A 503 that carries the server's own estimate. */
function hinted(retryAfterMs: number): JsonValue {
  return { status: 503, retryable: true, retryAfterMs };
}

const scenarios: ScenarioFile = {
  policy: "retry/retry-after-aware",
  cases: [
    {
      name: "smoke: the server's own estimate is used verbatim",
      params: REFERENCE,
      seed: 1,
      steps: [
        // No backoff curve, no draw: the server said 250 ms, so 250 ms it is.
        { call: "nextDelay", args: [1, hinted(250)], expect: 250 },
        { call: "nextDelay", args: [2, hinted(1_500)], expect: 1_500 },
        // The attempt number is irrelevant while a hint is present — the server
        // knows when it will be ready and the client does not.
        { call: "nextDelay", args: [7, hinted(300)], expect: 300 },
      ],
    },
    {
      name: "boundary: the hint is clamped, and zero means come back now",
      params: { baseMs: 100, capMs: 5_000, maxAttempts: 12 },
      seed: 1,
      steps: [
        // A server under load can ask for minutes. `capMs` is the caller's
        // statement of how long it is willing to be told to wait; past that the
        // request is advice rather than instruction.
        { call: "nextDelay", args: [1, hinted(5_000)], expect: 5_000 },
        { call: "nextDelay", args: [1, hinted(5_001)], expect: 5_000 },
        { call: "nextDelay", args: [1, hinted(600_000)], expect: 5_000 },
        // An absent hint and a hint of zero are different statements. Zero is
        // an instruction — retry immediately — and is honoured, which is why
        // the policy checks for undefined rather than for falsiness.
        { call: "nextDelay", args: [1, hinted(0)], expect: 0 },
      ],
    },
    {
      name: "distinguishing: falls back to full jitter only when nothing was said",
      // The same policy on the same attempt, once told and once not. The hinted
      // answer is exact and the unhinted one is a draw under the ceiling of
      // 400 for attempt 3 — which is the whole design: never worse than the
      // default, and much better when the server has bothered to tell you.
      params: REFERENCE,
      seed: 5,
      steps: [
        { call: "nextDelay", args: [3, hinted(777)], expect: 777 },
        { call: "nextDelay", args: [3, RETRYABLE], capture: true },
        { call: "nextDelay", args: [3, hinted(777)], expect: 777 },
        { call: "nextDelay", args: [3, RETRYABLE], capture: true },
      ],
    },
    {
      name: "tiebreak: the hinted path consumes no randomness",
      // A hinted call must leave the random stream exactly where it was, or a
      // port that drew anyway would diverge on the next fallback. Two hinted
      // calls are interleaved here and the fallback draws that follow are the
      // same ones the previous case produced from the same seed.
      params: REFERENCE,
      seed: 5,
      steps: [
        { call: "nextDelay", args: [1, hinted(10)], expect: 10 },
        { call: "nextDelay", args: [2, hinted(20)], expect: 20 },
        { call: "nextDelay", args: [3, RETRYABLE], capture: true },
        { call: "nextDelay", args: [4, hinted(30)], expect: 30 },
        { call: "nextDelay", args: [3, RETRYABLE], capture: true },
      ],
    },
    {
      name: "giving up ignores the hint entirely",
      params: { baseMs: 100, capMs: 10_000, maxAttempts: 3 },
      seed: 1,
      steps: [
        // A server asking a client to come back does not overrule the client's
        // own budget, and a permanent failure is permanent however politely it
        // suggests otherwise.
        { call: "nextDelay", args: [3, hinted(100)], expect: null },
        { call: "nextDelay", args: [1, { status: 400, retryable: false, retryAfterMs: 50 }], expect: null },
      ],
    },
  ],
};

export default scenarios;
