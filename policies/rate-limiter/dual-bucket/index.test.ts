import { describe, expect, it } from "vitest";
import { Rng } from "../../../packages/core/src/rng";
import { formatFailures, runVectors } from "../../../packages/vectors/src/run";
import type { VectorsFile } from "../../../packages/vectors/src/types";
import DualBucket from "./index";
import vectors from "./vectors.json";

describe("rate-limiter/dual-bucket", () => {
  it("passes its vectors", () => {
    const result = runVectors((params) => new DualBucket(params), vectors as VectorsFile);
    if (result.failures.length > 0) throw new Error(formatFailures(result));
    expect(result.assertionsRun).toBeGreaterThan(0);
  });

  it("rejects parameters that are not positive integers", () => {
    expect(() => new DualBucket({ requestsPerMin: 0 })).toThrow(RangeError);
    expect(() => new DualBucket({ requestsPerMin: 1.5 })).toThrow(RangeError);
    expect(() => new DualBucket({ tokensPerMin: 0 })).toThrow(RangeError);
    expect(() => new DualBucket({ tokensPerMin: -1 })).toThrow(RangeError);
  });

  it("defaults to a plausible LLM-API allowance", () => {
    const limiter = new DualBucket();
    expect(limiter.requestsOf(1, 0)).toBe(500);
    expect(limiter.tokensOf(1, 0)).toBe(200_000);
  });

  it("charges nothing when either dimension refuses", () => {
    // The property worth being most careful about: a caller refused for work
    // must not have quietly spent a request too, or retrying would throttle it
    // harder than not retrying.
    const rng = new Rng(5);
    const limiter = new DualBucket({ requestsPerMin: 60, tokensPerMin: 6_000 });

    let now = 0;
    for (let step = 0; step < 2_000; step += 1) {
      now += rng.nextInt(300);
      const before = { requests: limiter.requestsOf(1, now), tokens: limiter.tokensOf(1, now) };
      const cost = 1 + rng.nextInt(500);

      if (limiter.allow(1, cost, now)) {
        expect(limiter.requestsOf(1, now)).toBe(before.requests - 1);
        expect(limiter.tokensOf(1, now)).toBe(before.tokens - cost);
      } else {
        expect(limiter.requestsOf(1, now)).toBe(before.requests);
        expect(limiter.tokensOf(1, now)).toBe(before.tokens);
      }
    }
  });

  it("holds each dimension to its own ceiling over a long run", () => {
    // A minute of pressure with calls small enough that the request ceiling is
    // the binding one, then large enough that the work ceiling is.
    for (const [cost, expected] of [
      [1, 500],
      [1_000, 200],
    ] as const) {
      const limiter = new DualBucket();
      let admitted = 0;
      for (let now = 0; now < 60_000; now += 10) if (limiter.allow(1, cost, now)) admitted += 1;
      // Within one call of the ceiling: the bucket starts full and refills as
      // the minute runs, so the exact figure depends on where the last call
      // lands.
      expect(admitted).toBeGreaterThanOrEqual(expected);
      expect(admitted).toBeLessThanOrEqual(expected * 2);
    }
  });

  it("reports the later of the two waits", () => {
    // Requests are the scarce dimension here: three a minute is one every
    // twenty seconds, while a thousand units a minute is one every sixty
    // milliseconds. The wait has to be the one that actually binds.
    const limiter = new DualBucket({ requestsPerMin: 3, tokensPerMin: 1_000 });
    for (let i = 0; i < 3; i += 1) limiter.allow(1, 1, 0);
    expect(limiter.requestsOf(1, 0)).toBe(0);
    expect(limiter.tokensOf(1, 0)).toBe(997);
    expect(limiter.retryAfter(1, 0)).toBe(20_000);
    expect(limiter.allow(1, 1, 20_000)).toBe(true);
  });

  it("reports a wait that admits a minimal call when it elapses", () => {
    const rng = new Rng(13);
    const limiter = new DualBucket({ requestsPerMin: 30, tokensPerMin: 3_000 });
    let now = 0;
    let denials = 0;

    for (let step = 0; step < 500; step += 1) {
      now += rng.nextInt(500);
      if (limiter.allow(1, 1 + rng.nextInt(200), now)) continue;
      denials += 1;
      const wait = limiter.retryAfter(1, now);
      // The hint is for a smallest possible call, so that is what it promises.
      expect(limiter.allow(1, 1, now + wait)).toBe(true);
      now += wait;
    }
    expect(denials).toBeGreaterThan(20);
  });

  it("keeps five integers per key", () => {
    const limiter = new DualBucket();
    for (let key = 0; key < 500; key += 1) limiter.allow(key, 1, 0);
    expect(limiter.stateSize()).toBe(500);
  });
});
