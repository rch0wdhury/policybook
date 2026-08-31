import { describe, expect, it } from "vitest";
import { Rng } from "../../../packages/core/src/rng";
import { formatFailures, runVectors } from "../../../packages/vectors/src/run";
import type { VectorsFile } from "../../../packages/vectors/src/types";
import { backoffCeiling } from "../exponential/index";
import EqualJitter from "./index";
import vectors from "./vectors.json";

const RETRYABLE = { status: 503, retryable: true };

describe("retry/equal-jitter", () => {
  it("passes its vectors", () => {
    const result = runVectors(
      (params, rng) => new EqualJitter(params, rng),
      vectors as VectorsFile,
    );
    if (result.failures.length > 0) throw new Error(formatFailures(result));
    expect(result.assertionsRun).toBeGreaterThan(0);
  });

  it("rejects parameters that are out of range", () => {
    expect(() => new EqualJitter({ baseMs: 0 })).toThrow(RangeError);
    expect(() => new EqualJitter({ capMs: 0 })).toThrow(RangeError);
    expect(() => new EqualJitter({ maxAttempts: 0 })).toThrow(RangeError);
  });

  it("always lands between half the ceiling and the whole of it", () => {
    // The defining property, and the one a captured vector cannot state.
    const params = { baseMs: 100, capMs: 10_000, maxAttempts: 40 };
    let reachedFloor = false;
    let reachedCeiling = false;

    for (let seed = 0; seed < 150; seed += 1) {
      const policy = new EqualJitter(params, new Rng(seed));
      for (let attempt = 1; attempt <= 10; attempt += 1) {
        const ceiling = backoffCeiling(attempt, params.baseMs, params.capMs);
        const half = Math.floor(ceiling / 2);
        const delay = policy.nextDelay(attempt, RETRYABLE)!;

        expect(delay).toBeGreaterThanOrEqual(half);
        expect(delay).toBeLessThanOrEqual(2 * half);
        if (delay === half) reachedFloor = true;
        if (delay === 2 * half) reachedCeiling = true;
      }
    }
    // Both ends of `[half, 2 x half]` occur: `half + nextInt(half + 1)`, not
    // `nextInt(half)` at either end.
    expect(reachedFloor).toBe(true);
    expect(reachedCeiling).toBe(true);
  });

  it("guarantees a floor where full jitter guarantees nothing", () => {
    // The reason to choose it. Over many attempts full jitter can return zero
    // repeatedly; this cannot fall below half the ceiling, so the backoff
    // really does back off.
    const policy = new EqualJitter({ baseMs: 100, capMs: 10_000, maxAttempts: 200 });
    let smallest = Number.POSITIVE_INFINITY;
    for (let attempt = 1; attempt <= 100; attempt += 1) {
      const delay = policy.nextDelay(attempt, RETRYABLE)!;
      if (delay < smallest) smallest = delay;
    }
    expect(smallest).toBeGreaterThanOrEqual(50);
  });

  it("averages about three quarters of the un-jittered delay", () => {
    // Between full jitter's half and plain exponential's whole, which is the
    // trade it exists to make.
    const params = { baseMs: 1_000, capMs: 1_000, maxAttempts: 5_000 };
    const policy = new EqualJitter(params, new Rng(4));
    let total = 0;
    const draws = 4_000;
    for (let i = 0; i < draws; i += 1) total += policy.nextDelay(1, RETRYABLE)!;

    expect(total / draws).toBeGreaterThan(700);
    expect(total / draws).toBeLessThan(800);
  });

  it("gives up without drawing", () => {
    const params = { baseMs: 100, capMs: 10_000, maxAttempts: 3 };
    const drew = new EqualJitter(params, new Rng(9));
    const refused = new EqualJitter(params, new Rng(9));

    expect(refused.nextDelay(3, RETRYABLE)).toBeNull();
    expect(refused.nextDelay(1, { status: 400, retryable: false })).toBeNull();
    expect(refused.nextDelay(1, RETRYABLE)).toBe(drew.nextDelay(1, RETRYABLE));
  });
});
