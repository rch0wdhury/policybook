import { describe, expect, it } from "vitest";
import { Rng } from "../../../packages/core/src/rng";
import { formatFailures, runVectors } from "../../../packages/vectors/src/run";
import type { VectorsFile } from "../../../packages/vectors/src/types";
import DecorrelatedJitter from "./index";
import vectors from "./vectors.json";

const RETRYABLE = { status: 503, retryable: true };

describe("retry/decorrelated-jitter", () => {
  it("passes its vectors", () => {
    const result = runVectors(
      (params, rng) => new DecorrelatedJitter(params, rng),
      vectors as VectorsFile,
    );
    if (result.failures.length > 0) throw new Error(formatFailures(result));
    expect(result.assertionsRun).toBeGreaterThan(0);
  });

  it("rejects parameters that are out of range", () => {
    expect(() => new DecorrelatedJitter({ baseMs: 0 })).toThrow(RangeError);
    expect(() => new DecorrelatedJitter({ capMs: 0 })).toThrow(RangeError);
    expect(() => new DecorrelatedJitter({ maxAttempts: 0 })).toThrow(RangeError);
  });

  it("stays inside [base, min(cap, 3 x previous)] on every step", () => {
    // The formula's bounds, checked against the state the policy reports.
    const params = { baseMs: 100, capMs: 10_000, maxAttempts: 5_000 };
    for (let seed = 0; seed < 100; seed += 1) {
      const policy = new DecorrelatedJitter(params, new Rng(seed));
      let previous = params.baseMs;

      for (let attempt = 1; attempt <= 40; attempt += 1) {
        const delay = policy.nextDelay(attempt, RETRYABLE)!;
        expect(delay).toBeGreaterThanOrEqual(params.baseMs);
        expect(delay).toBeLessThanOrEqual(Math.min(params.capMs, previous * 3));
        // The state advances to the delay actually used, cap included.
        expect(policy.previousDelay()).toBe(delay);
        previous = delay;
      }
    }
  });

  it("ignores the attempt number entirely", () => {
    // Every other policy here answers identically to a repeated attempt
    // number. This one walks, so the same question gets different answers —
    // and, more to the point, an ascending attempt number and a fixed one
    // produce the same distribution of walks.
    const params = { baseMs: 100, capMs: 10_000, maxAttempts: 5_000 };
    const walking = new DecorrelatedJitter(params, new Rng(3));
    const fixed = new DecorrelatedJitter(params, new Rng(3));

    for (let step = 1; step <= 30; step += 1) {
      expect(fixed.nextDelay(1, RETRYABLE)).toBe(walking.nextDelay(step, RETRYABLE));
    }
  });

  it("saturates at the cap and advances the walk to the capped value", () => {
    // Without capping the state, a client at the ceiling would keep drawing
    // from an ever-growing range it can never reach — the delay would be
    // pinned at the cap but the *distribution* would keep climbing, which is
    // meaningless state.
    const policy = new DecorrelatedJitter(
      { baseMs: 100, capMs: 150, maxAttempts: 200 },
      new Rng(5),
    );
    for (let attempt = 1; attempt <= 30; attempt += 1) {
      const delay = policy.nextDelay(attempt, RETRYABLE)!;
      expect(delay).toBeLessThanOrEqual(150);
      expect(policy.previousDelay()).toBeLessThanOrEqual(150);
    }
  });

  it("grows by about 1.5x a step, not the 3x the range suggests", () => {
    // Easy to get wrong, and I did: the draw reaches 3 x previous, so the
    // policy looks more aggressive than exponential. But it is uniform over
    // that range, so the expected step is (base + 3 x prev) / 2 — about 1.5x,
    // which is *slower* than exponential's exact 2x.
    const params = { baseMs: 100, capMs: 10_000_000, maxAttempts: 5_000 };
    const ratios: number[] = [];

    for (let seed = 0; seed < 300; seed += 1) {
      const policy = new DecorrelatedJitter(params, new Rng(seed));
      let previous = params.baseMs;
      for (let attempt = 1; attempt <= 12; attempt += 1) {
        const delay = policy.nextDelay(attempt, RETRYABLE)!;
        // Skip the first few steps, where the `base` term still dominates.
        if (previous > 1_000) ratios.push(delay / previous);
        previous = delay;
      }
    }

    const mean = ratios.reduce((sum, r) => sum + r, 0) / ratios.length;
    expect(ratios.length).toBeGreaterThan(1_000);
    expect(mean).toBeGreaterThan(1.4);
    expect(mean).toBeLessThan(1.65);
  });

  it("takes wildly different numbers of steps to reach the cap", () => {
    // The variance is the point, not the speed. Exponential reaches the cap at
    // attempt 8 for every client; this takes a median of 14 and ranges from
    // single digits to the high tens, so two clients that started together are
    // at very different delays a few attempts in.
    const params = { baseMs: 100, capMs: 10_000, maxAttempts: 500 };
    const stepsToCap: number[] = [];

    for (let seed = 0; seed < 300; seed += 1) {
      const policy = new DecorrelatedJitter(params, new Rng(seed));
      for (let attempt = 1; attempt <= 200; attempt += 1) {
        if (policy.nextDelay(attempt, RETRYABLE) === params.capMs) {
          stepsToCap.push(attempt);
          break;
        }
      }
    }

    stepsToCap.sort((a, b) => a - b);
    expect(stepsToCap.length).toBeGreaterThan(250);

    const median = stepsToCap[Math.floor(stepsToCap.length / 2)]!;
    // Slower than exponential's 8, which is the correction this test exists to
    // record.
    expect(median).toBeGreaterThan(8);
    expect(median).toBeLessThan(25);
    // And spread over at least a factor of three between the luckiest and the
    // unluckiest client.
    expect(stepsToCap[stepsToCap.length - 1]! / stepsToCap[0]!).toBeGreaterThan(3);
  });

  it("gives up without drawing or stepping the walk", () => {
    const params = { baseMs: 100, capMs: 10_000, maxAttempts: 3 };
    const policy = new DecorrelatedJitter(params, new Rng(9));

    expect(policy.nextDelay(3, RETRYABLE)).toBeNull();
    expect(policy.nextDelay(1, { status: 400, retryable: false })).toBeNull();
    expect(policy.previousDelay()).toBe(100);
  });
});
