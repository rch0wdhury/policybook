/**
 * Scenario script for retry/decorrelated-jitter.
 *
 * This is the only stateful policy in the domain, so the vectors are as much
 * about the walk as about the values: `previousDelay` is read between calls to
 * show the state advancing, and the distinguishing case asks the same attempt
 * number repeatedly to show that the answer depends on history rather than on
 * the attempt.
 *
 * Regenerate with: pnpm gen:vectors retry/decorrelated-jitter
 */

import type { ScenarioFile } from "../../../packages/vectors/src/gen";

const RETRYABLE = { status: 503, retryable: true };
const REFERENCE = { baseMs: 100, capMs: 10_000, maxAttempts: 12 };

const scenarios: ScenarioFile = {
  policy: "retry/decorrelated-jitter",
  cases: [
    {
      name: "smoke: the walk starts at base and climbs from its own last step",
      params: REFERENCE,
      seed: 1,
      steps: [
        // Before any call the walk sits at `baseMs`, which is where it starts.
        { call: "previousDelay", expect: 100 },
        { call: "nextDelay", args: [1, RETRYABLE], capture: true },
        // The state is now whatever that returned, and the next draw reaches
        // up to three times it.
        { call: "previousDelay", capture: true },
        { call: "nextDelay", args: [2, RETRYABLE], capture: true },
        { call: "previousDelay", capture: true },
        { call: "nextDelay", args: [3, RETRYABLE], capture: true },
        { call: "previousDelay", capture: true },
        { call: "nextDelay", args: [4, RETRYABLE], capture: true },
        { call: "previousDelay", capture: true },
      ],
    },
    {
      name: "boundary: the walk stops at the cap and stays there",
      // With a cap barely above the base the walk saturates on its first step
      // and every later delay is the cap. The state advances to the *capped*
      // value, not to the uncapped draw — otherwise a client at the cap would
      // keep drawing from a range it can never reach.
      params: { baseMs: 100, capMs: 120, maxAttempts: 10 },
      seed: 5,
      steps: [
        { call: "nextDelay", args: [1, RETRYABLE], capture: true },
        { call: "nextDelay", args: [2, RETRYABLE], capture: true },
        { call: "nextDelay", args: [3, RETRYABLE], capture: true },
        { call: "nextDelay", args: [4, RETRYABLE], capture: true },
        { call: "previousDelay", capture: true },
      ],
    },
    {
      name: "distinguishing: the attempt number does not determine the delay",
      // Every other policy here answers identically to a repeated attempt
      // number — `exponential` returns 400 to all of these, and `full-jitter`
      // draws from the same fixed ceiling each time. This one walks, so the
      // range itself moves between calls. That is a stronger decorrelation:
      // whole schedules diverge rather than individual attempts.
      params: REFERENCE,
      seed: 7,
      steps: [
        { call: "nextDelay", args: [3, RETRYABLE], capture: true },
        { call: "nextDelay", args: [3, RETRYABLE], capture: true },
        { call: "nextDelay", args: [3, RETRYABLE], capture: true },
        { call: "nextDelay", args: [3, RETRYABLE], capture: true },
        { call: "previousDelay", capture: true },
      ],
    },
    {
      name: "tiebreak: give-up beats the draw, and leaves the walk untouched",
      params: { baseMs: 100, capMs: 10_000, maxAttempts: 3 },
      seed: 1,
      steps: [
        { call: "nextDelay", args: [3, RETRYABLE], expect: null },
        { call: "nextDelay", args: [1, { status: 400, retryable: false }], expect: null },
        // The refusals consumed neither a draw nor a step of the walk, so the
        // state is still exactly where it started.
        { call: "previousDelay", expect: 100 },
        { call: "nextDelay", args: [1, RETRYABLE], capture: true },
      ],
    },
  ],
};

export default scenarios;
