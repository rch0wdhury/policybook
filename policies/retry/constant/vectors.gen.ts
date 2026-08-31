/**
 * Scenario script for retry/constant.
 *
 * `nextDelay(attempt, error)` returns the milliseconds to wait before the next
 * attempt, or null to give up. `attempt` is 1-based and is the number of the
 * attempt that just failed.
 *
 * The Rng is supplied at construction, as it is for every policy in the
 * registry (see the domain interface for why this departs from concept.md
 * §5.1). This policy never draws from it.
 *
 * Regenerate with: pnpm gen:vectors retry/constant
 */

import type { ScenarioFile } from "../../../packages/vectors/src/gen";

const RETRYABLE = { status: 503, retryable: true };
const SMALL = { baseMs: 100, maxAttempts: 4 };

const scenarios: ScenarioFile = {
  policy: "retry/constant",
  cases: [
    {
      name: "smoke: the same delay every time, then give up",
      params: SMALL,
      seed: 1,
      steps: [
        { call: "nextDelay", args: [1, RETRYABLE], expect: 100 },
        { call: "nextDelay", args: [2, RETRYABLE], expect: 100 },
        { call: "nextDelay", args: [3, RETRYABLE], expect: 100 },
        // The fourth attempt has failed and `maxAttempts` is four, so there is
        // no fifth: null is the give-up signal, not an error.
        { call: "nextDelay", args: [4, RETRYABLE], expect: null },
      ],
    },
    {
      name: "boundary: give-up is at maxAttempts, not one either side of it",
      params: { baseMs: 250, maxAttempts: 1 },
      seed: 1,
      steps: [
        // With one attempt allowed, the first failure is already the last.
        { call: "nextDelay", args: [1, RETRYABLE], expect: null },
      ],
    },
    {
      name: "distinguishing: the delay never changes, however long the outage runs",
      // This is the whole difference from exponential backoff, and it is the
      // reason not to use this policy: after eight failures it is still asking
      // every hundred milliseconds, so a service that is down because it is
      // overloaded gets no relief at all.
      params: { baseMs: 100, maxAttempts: 10 },
      seed: 1,
      steps: [
        { call: "nextDelay", args: [1, RETRYABLE], expect: 100 },
        { call: "nextDelay", args: [5, RETRYABLE], expect: 100 },
        { call: "nextDelay", args: [9, RETRYABLE], expect: 100 },
      ],
    },
    {
      name: "tiebreak: a non-retryable failure gives up whatever the attempt number",
      params: SMALL,
      seed: 1,
      steps: [
        // Nothing is gained by retrying a failure the server calls permanent,
        // and the attempt budget is irrelevant to that.
        { call: "nextDelay", args: [1, { status: 400, retryable: false }], expect: null },
        { call: "nextDelay", args: [1, RETRYABLE], expect: 100 },
      ],
    },
    {
      name: "a zero delay is a legitimate configuration",
      params: { baseMs: 0, maxAttempts: 3 },
      seed: 1,
      steps: [
        { call: "nextDelay", args: [1, RETRYABLE], expect: 0 },
        { call: "nextDelay", args: [2, RETRYABLE], expect: 0 },
        { call: "nextDelay", args: [3, RETRYABLE], expect: null },
      ],
    },
  ],
};

export default scenarios;
