/**
 * Scenario script for retry/exponential-full-jitter.
 *
 * `nextDelay(attempt, error)` returns the milliseconds to wait, or null to give
 * up. The Rng is supplied at construction and seeded from the case's `seed`, so
 * the sequence below is reproducible in every language.
 *
 * **The delays are captured rather than hand-authored**, and deliberately so.
 * Which value a uniform draw returns is a fact about the shared Rng, not
 * something derivable from the paper — hand-writing them would be copying
 * numbers out of a debugger and calling it reasoning. What *is* hand-authored
 * is every decision the policy makes rather than draws: the give-up cases
 * below, which must not consume randomness at all.
 *
 * The properties a captured value cannot state — that each delay falls within
 * its exponential ceiling, that the ceiling doubles, that both ends of the
 * range are reachable — are asserted in `index.test.ts`, because a vector can
 * only pin an exact value and these are claims about a distribution.
 *
 * Regenerate with: pnpm gen:vectors retry/exponential-full-jitter
 */

import type { ScenarioFile } from "../../../packages/vectors/src/gen";

const RETRYABLE = { status: 503, retryable: true };
const REFERENCE = { baseMs: 100, capMs: 10_000, maxAttempts: 12 };

const scenarios: ScenarioFile = {
  policy: "retry/exponential-full-jitter",
  cases: [
    {
      name: "smoke: a uniform draw under a doubling ceiling",
      params: REFERENCE,
      seed: 1,
      steps: [
        // Ceilings 100, 200, 400, 800, 1600, 3200, 6400 — the same sequence
        // `retry/exponential` returns outright. Each delay is somewhere in
        // [0, ceiling].
        { call: "nextDelay", args: [1, RETRYABLE], capture: true },
        { call: "nextDelay", args: [2, RETRYABLE], capture: true },
        { call: "nextDelay", args: [3, RETRYABLE], capture: true },
        { call: "nextDelay", args: [4, RETRYABLE], capture: true },
        { call: "nextDelay", args: [5, RETRYABLE], capture: true },
        { call: "nextDelay", args: [6, RETRYABLE], capture: true },
        { call: "nextDelay", args: [7, RETRYABLE], capture: true },
      ],
    },
    {
      name: "boundary: the ceiling caps, and a delay of zero is reachable",
      params: { baseMs: 1, capMs: 1, maxAttempts: 6 },
      seed: 3,
      steps: [
        // A ceiling of one means `nextInt(2)`: every delay is 0 or 1, and both
        // occur. Zero is not a bug — a client retrying immediately is part of
        // what makes the arrival pattern smooth rather than merely delayed.
        { call: "nextDelay", args: [1, RETRYABLE], capture: true },
        { call: "nextDelay", args: [2, RETRYABLE], capture: true },
        { call: "nextDelay", args: [3, RETRYABLE], capture: true },
        { call: "nextDelay", args: [4, RETRYABLE], capture: true },
        { call: "nextDelay", args: [5, RETRYABLE], capture: true },
      ],
    },
    {
      name: "distinguishing: the same attempt number gives different delays",
      // The whole difference from `retry/exponential`, which answers 400 to
      // every one of these. Two clients that failed together do not come back
      // together, and that is the entire contribution of the policy.
      params: REFERENCE,
      seed: 7,
      steps: [
        { call: "nextDelay", args: [3, RETRYABLE], capture: true },
        { call: "nextDelay", args: [3, RETRYABLE], capture: true },
        { call: "nextDelay", args: [3, RETRYABLE], capture: true },
        { call: "nextDelay", args: [3, RETRYABLE], capture: true },
      ],
    },
    {
      name: "tiebreak: give-up beats the draw, at maxAttempts and on a permanent failure",
      // Hand-authored, because these are decisions rather than draws: the
      // policy must not consume randomness on a call it is going to refuse.
      params: { baseMs: 100, capMs: 10_000, maxAttempts: 3 },
      seed: 1,
      steps: [
        { call: "nextDelay", args: [3, RETRYABLE], expect: null },
        { call: "nextDelay", args: [1, { status: 400, retryable: false }], expect: null },
        { call: "nextDelay", args: [9, RETRYABLE], expect: null },
      ],
    },
  ],
};

export default scenarios;
