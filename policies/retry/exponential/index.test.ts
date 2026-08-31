import { describe, expect, it } from "vitest";
import { formatFailures, runVectors } from "../../../packages/vectors/src/run";
import type { VectorsFile } from "../../../packages/vectors/src/types";
import Exponential, { backoffCeiling } from "./index";
import vectors from "./vectors.json";

const RETRYABLE = { status: 503, retryable: true };

describe("retry/exponential", () => {
  it("passes its vectors", () => {
    const result = runVectors((params) => new Exponential(params), vectors as VectorsFile);
    if (result.failures.length > 0) throw new Error(formatFailures(result));
    expect(result.assertionsRun).toBeGreaterThan(0);
  });

  it("rejects parameters that are out of range", () => {
    expect(() => new Exponential({ baseMs: 0 })).toThrow(RangeError);
    expect(() => new Exponential({ capMs: 0 })).toThrow(RangeError);
    expect(() => new Exponential({ maxAttempts: 0 })).toThrow(RangeError);
  });

  it("defaults to the registry's reference configuration", () => {
    const policy = new Exponential();
    expect(policy.nextDelay(1, RETRYABLE)).toBe(100);
    expect(policy.nextDelay(7, RETRYABLE)).toBe(6_400);
    expect(policy.nextDelay(8, RETRYABLE)).toBeNull();
  });

  it("covers about 12.7 seconds at the reference configuration", () => {
    // The number that explains the benchmark: eight attempts of exponential
    // backoff from a 100 ms base is twelve and a half seconds of patience,
    // against outages of up to thirty. Most episodes end in failure, for every
    // policy, and that is the honest result rather than a broken harness.
    const policy = new Exponential();
    let total = 0;
    for (let attempt = 1; ; attempt += 1) {
      const delay = policy.nextDelay(attempt, RETRYABLE);
      if (delay === null) break;
      total += delay;
    }
    expect(total).toBe(12_700);
  });

  describe("backoffCeiling", () => {
    it("doubles until the cap and then stops", () => {
      expect([1, 2, 3, 4, 5].map((n) => backoffCeiling(n, 100, 10_000))).toEqual([
        100, 200, 400, 800, 1_600,
      ]);
      expect(backoffCeiling(8, 100, 10_000)).toBe(10_000);
      expect(backoffCeiling(9, 100, 10_000)).toBe(10_000);
    });

    it("cannot overflow however large the attempt number", () => {
      // The doubling stops as soon as the cap is reached, so the loop does a
      // bounded number of multiplications rather than shifting past the width
      // of the integer. A naive `base << (attempt - 1)` would be nonsense here.
      expect(backoffCeiling(1_000_000, 100, 10_000)).toBe(10_000);
      expect(Number.isSafeInteger(backoffCeiling(1_000_000, 100, 10_000))).toBe(true);
    });

    it("clamps from the first attempt when the cap is below the base", () => {
      expect(backoffCeiling(1, 5_000, 1_000)).toBe(1_000);
    });
  });

  it("answers identically however many times it is asked", () => {
    // The difference from full jitter, stated directly: two clients that failed
    // together get the same delay, so they come back together.
    const policy = new Exponential();
    const answers = new Set<number | null>();
    for (let i = 0; i < 20; i += 1) answers.add(policy.nextDelay(4, RETRYABLE));
    expect(answers).toEqual(new Set([800]));
  });
});
