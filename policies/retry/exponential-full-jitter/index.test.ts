import { describe, expect, it } from "vitest";
import { Rng } from "../../../packages/core/src/rng";
import { formatFailures, runVectors } from "../../../packages/vectors/src/run";
import type { VectorsFile } from "../../../packages/vectors/src/types";
import { backoffCeiling } from "../exponential/index";
import ExponentialFullJitter from "./index";
// The policy restates `backoffCeiling` rather than importing it, so that a
// copied file compiles on its own. `backoff-policies.test.ts` asserts the two
// copies agree; here the exported one is used as the reference.
import vectors from "./vectors.json";

const RETRYABLE = { status: 503, retryable: true };

describe("retry/exponential-full-jitter", () => {
  it("passes its vectors", () => {
    const result = runVectors(
      (params, rng) => new ExponentialFullJitter(params, rng),
      vectors as VectorsFile,
    );
    if (result.failures.length > 0) throw new Error(formatFailures(result));
    expect(result.assertionsRun).toBeGreaterThan(0);
  });

  it("rejects parameters that are not positive integers", () => {
    expect(() => new ExponentialFullJitter({ baseMs: 0 })).toThrow(RangeError);
    expect(() => new ExponentialFullJitter({ capMs: 0 })).toThrow(RangeError);
    expect(() => new ExponentialFullJitter({ maxAttempts: 0 })).toThrow(RangeError);
    expect(() => new ExponentialFullJitter({ baseMs: 1.5 })).toThrow(RangeError);
  });

  // The claims a captured vector cannot make, because they are about a
  // distribution rather than a value.
  describe("the draw", () => {
    it("never exceeds the exponential ceiling, and can reach it", () => {
      const params = { baseMs: 100, capMs: 10_000, maxAttempts: 30 };
      let reachedCeiling = false;

      for (let seed = 0; seed < 200; seed += 1) {
        const policy = new ExponentialFullJitter(params, new Rng(seed));
        for (let attempt = 1; attempt <= 12; attempt += 1) {
          const ceiling = backoffCeiling(attempt, params.baseMs, params.capMs);
          const delay = policy.nextDelay(attempt, RETRYABLE);

          expect(delay).not.toBeNull();
          expect(delay!).toBeGreaterThanOrEqual(0);
          expect(delay!).toBeLessThanOrEqual(ceiling);
          if (delay === ceiling) reachedCeiling = true;
        }
      }

      // `nextInt(ceiling + 1)` rather than `nextInt(ceiling)`: the ceiling
      // itself is a legal delay, and a port that got the bound wrong would
      // never produce it.
      expect(reachedCeiling).toBe(true);
    });

    it("reaches zero, which is a real strategy rather than a bug", () => {
      // Some client retrying immediately is part of what spreads the arrivals
      // out. A policy that never returned zero would be shifting the herd
      // rather than dispersing it.
      const policy = new ExponentialFullJitter(
        { baseMs: 2, capMs: 2, maxAttempts: 100 },
        new Rng(11),
      );
      const seen = new Set<number | null>();
      for (let attempt = 1; attempt < 60; attempt += 1) {
        seen.add(policy.nextDelay(attempt, RETRYABLE));
      }
      expect(seen).toEqual(new Set([0, 1, 2]));
    });

    it("averages about half the un-jittered delay", () => {
      // The consequence people are most often surprised by: full jitter is
      // *more* aggressive than plain exponential, not less. It wins anyway
      // because spreading matters more than the average.
      const params = { baseMs: 1_000, capMs: 1_000, maxAttempts: 5_000 };
      const policy = new ExponentialFullJitter(params, new Rng(4));

      let total = 0;
      const draws = 4_000;
      for (let i = 0; i < draws; i += 1) total += policy.nextDelay(1, RETRYABLE)!;

      expect(total / draws).toBeGreaterThan(450);
      expect(total / draws).toBeLessThan(550);
    });

    it("spreads two clients that failed together", () => {
      // Two policies with different streams, asked the identical question.
      // Plain exponential would answer identically every time.
      const params = { baseMs: 1_000, capMs: 1_000, maxAttempts: 100 };
      const left = new ExponentialFullJitter(params, new Rng(1));
      const right = new ExponentialFullJitter(params, new Rng(2));

      let agreements = 0;
      for (let attempt = 1; attempt <= 50; attempt += 1) {
        if (left.nextDelay(attempt, RETRYABLE) === right.nextDelay(attempt, RETRYABLE)) {
          agreements += 1;
        }
      }
      // Coincidences are possible out of 1,001 values; lockstep is not.
      expect(agreements).toBeLessThan(3);
    });
  });

  describe("giving up", () => {
    it("consumes no randomness on a call it refuses", () => {
      // A policy that drew before deciding to give up would leave the stream in
      // a different place, and a port that ordered it differently would
      // silently diverge from here on.
      const params = { baseMs: 100, capMs: 10_000, maxAttempts: 3 };
      const drew = new ExponentialFullJitter(params, new Rng(9));
      const refused = new ExponentialFullJitter(params, new Rng(9));

      expect(refused.nextDelay(3, RETRYABLE)).toBeNull();
      expect(refused.nextDelay(1, { status: 400, retryable: false })).toBeNull();
      // Both policies are still at the same point in their streams.
      expect(refused.nextDelay(1, RETRYABLE)).toBe(drew.nextDelay(1, RETRYABLE));
    });

    it("gives up at maxAttempts and on a permanent failure", () => {
      const policy = new ExponentialFullJitter(
        { baseMs: 100, capMs: 10_000, maxAttempts: 2 },
        new Rng(1),
      );
      expect(policy.nextDelay(1, RETRYABLE)).not.toBeNull();
      expect(policy.nextDelay(2, RETRYABLE)).toBeNull();
      expect(policy.nextDelay(1, { status: 400, retryable: false })).toBeNull();
    });
  });
});
