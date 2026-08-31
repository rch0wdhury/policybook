/**
 * Scenario script for retry/equal-jitter.
 *
 * The draws are captured — which value a uniform draw returns is a fact about
 * the shared Rng — while every decision the policy makes rather than draws is
 * hand-authored. The property a captured value cannot state, that each delay
 * lands in `[half, 2 x half]`, is asserted in `index.test.ts`.
 *
 * Regenerate with: pnpm gen:vectors retry/equal-jitter
 */

import type { ScenarioFile } from "../../../packages/vectors/src/gen";

const RETRYABLE = { status: 503, retryable: true };
const REFERENCE = { baseMs: 100, capMs: 10_000, maxAttempts: 12 };

const scenarios: ScenarioFile = {
  policy: "retry/equal-jitter",
  cases: [
    {
      name: "smoke: half the ceiling fixed, half drawn",
      params: REFERENCE,
      seed: 1,
      steps: [
        // Ceilings 100, 200, 400, 800, 1600, 3200, 6400 — so halves of 50, 100,
        // 200, 400, 800, 1600, 3200, and each delay lands between its half and
        // twice it.
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
      name: "boundary: a ceiling of one halves to zero and the delay degenerates",
      // Integer halving, stated rather than hidden. At `baseMs` 1 the half is 0
      // and `0 + nextInt(1)` is always 0, so the policy stops backing off
      // entirely. It is what the formula says; a base of one millisecond is not
      // a configuration to rely on, and the vector exists so nobody discovers
      // this in production.
      params: { baseMs: 1, capMs: 1, maxAttempts: 6 },
      seed: 3,
      steps: [
        { call: "nextDelay", args: [1, RETRYABLE], expect: 0 },
        { call: "nextDelay", args: [2, RETRYABLE], expect: 0 },
        { call: "nextDelay", args: [3, RETRYABLE], expect: 0 },
      ],
    },
    {
      name: "distinguishing: never below half the ceiling, where full jitter can be zero",
      // The reason to choose this over full jitter. Ceiling 100 at attempt one,
      // so every delay here is at least 50 — full jitter's can be 0, and over
      // eight attempts a run of small draws leaves a struggling service with no
      // respite at all.
      params: REFERENCE,
      seed: 11,
      steps: [
        { call: "nextDelay", args: [1, RETRYABLE], capture: true },
        { call: "nextDelay", args: [1, RETRYABLE], capture: true },
        { call: "nextDelay", args: [1, RETRYABLE], capture: true },
        { call: "nextDelay", args: [1, RETRYABLE], capture: true },
        { call: "nextDelay", args: [1, RETRYABLE], capture: true },
      ],
    },
    {
      name: "tiebreak: give-up beats the draw, at maxAttempts and on a permanent failure",
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
