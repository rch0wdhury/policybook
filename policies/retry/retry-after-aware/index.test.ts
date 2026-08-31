import { describe, expect, it } from "vitest";
import { Rng } from "../../../packages/core/src/rng";
import { formatFailures, runVectors } from "../../../packages/vectors/src/run";
import type { VectorsFile } from "../../../packages/vectors/src/types";
import { backoffCeiling } from "../exponential/index";
import ExponentialFullJitter from "../exponential-full-jitter/index";
import RetryAfterAware from "./index";
import vectors from "./vectors.json";

const RETRYABLE = { status: 503, retryable: true };
const hinted = (retryAfterMs: number) => ({ status: 503, retryable: true, retryAfterMs });

describe("retry/retry-after-aware", () => {
  it("passes its vectors", () => {
    const result = runVectors(
      (params, rng) => new RetryAfterAware(params, rng),
      vectors as VectorsFile,
    );
    if (result.failures.length > 0) throw new Error(formatFailures(result));
    expect(result.assertionsRun).toBeGreaterThan(0);
  });

  it("rejects parameters that are out of range", () => {
    expect(() => new RetryAfterAware({ baseMs: 0 })).toThrow(RangeError);
    expect(() => new RetryAfterAware({ capMs: 0 })).toThrow(RangeError);
    expect(() => new RetryAfterAware({ maxAttempts: 0 })).toThrow(RangeError);
  });

  it("rejects a hint that is not a non-negative integer", () => {
    // A malformed `Retry-After` is a bug in the caller's parsing, and turning
    // it into a fractional or negative delay would surface far from here.
    const policy = new RetryAfterAware();
    for (const bad of [-1, 1.5, Number.NaN]) {
      expect(() => policy.nextDelay(1, hinted(bad))).toThrow(RangeError);
    }
  });

  it("is exactly full jitter when no hint arrives", () => {
    // The fallback claim, checked against the policy it claims to fall back
    // to rather than asserted. Same seed, same answers, every time.
    const params = { baseMs: 100, capMs: 10_000, maxAttempts: 500 };
    const aware = new RetryAfterAware(params, new Rng(17));
    const jitter = new ExponentialFullJitter(params, new Rng(17));

    for (let attempt = 1; attempt <= 60; attempt += 1) {
      expect(aware.nextDelay(attempt, RETRYABLE)).toBe(jitter.nextDelay(attempt, RETRYABLE));
    }
  });

  it("consumes no randomness on the hinted path", () => {
    // Interleaving hinted calls must not shift the fallback stream, or a port
    // that drew anyway would diverge on the next unhinted failure.
    const params = { baseMs: 100, capMs: 10_000, maxAttempts: 500 };
    const interleaved = new RetryAfterAware(params, new Rng(23));
    const clean = new RetryAfterAware(params, new Rng(23));

    for (let attempt = 1; attempt <= 40; attempt += 1) {
      interleaved.nextDelay(attempt, hinted(attempt * 7));
      interleaved.nextDelay(attempt, hinted(0));
      expect(interleaved.nextDelay(attempt, RETRYABLE)).toBe(
        clean.nextDelay(attempt, RETRYABLE),
      );
    }
  });

  it("never waits longer than the cap, whatever the server asks", () => {
    // The clamp is what stops a stranger setting this client's latency budget.
    const policy = new RetryAfterAware({ baseMs: 100, capMs: 2_000, maxAttempts: 500 });
    for (const ask of [0, 1, 1_999, 2_000, 2_001, 600_000, 86_400_000]) {
      expect(policy.nextDelay(1, hinted(ask))).toBe(Math.min(2_000, ask));
    }
  });

  it("never exceeds the fallback ceiling when unhinted", () => {
    const params = { baseMs: 100, capMs: 10_000, maxAttempts: 500 };
    const policy = new RetryAfterAware(params, new Rng(2));
    for (let attempt = 1; attempt <= 20; attempt += 1) {
      const ceiling = backoffCeiling(attempt, params.baseMs, params.capMs);
      expect(policy.nextDelay(attempt, RETRYABLE)!).toBeLessThanOrEqual(ceiling);
    }
  });

  it("re-synchronises clients that were told the same thing", () => {
    // The honest cost, asserted rather than only described. A fleet given the
    // same hint returns as one — which is precisely what jitter exists to
    // prevent, and why this policy is not the domain's default.
    const params = { baseMs: 100, capMs: 10_000, maxAttempts: 500 };
    const fleet = Array.from({ length: 50 }, (_, i) => new RetryAfterAware(params, new Rng(i)));
    const delays = new Set(fleet.map((p) => p.nextDelay(1, hinted(5_000))));
    expect(delays).toEqual(new Set([5_000]));

    // Without a hint the same fleet spreads out.
    const spread = new Set(fleet.map((p) => p.nextDelay(1, RETRYABLE)));
    expect(spread.size).toBeGreaterThan(20);
  });

  it("gives up regardless of what the server suggests", () => {
    const policy = new RetryAfterAware({ baseMs: 100, capMs: 10_000, maxAttempts: 3 });
    expect(policy.nextDelay(3, hinted(100))).toBeNull();
    expect(
      policy.nextDelay(1, { status: 400, retryable: false, retryAfterMs: 50 }),
    ).toBeNull();
  });
});
