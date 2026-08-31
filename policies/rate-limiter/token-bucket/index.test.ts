import { describe, expect, it } from "vitest";
import { formatFailures, runVectors } from "../../../packages/vectors/src/run";
import type { VectorsFile } from "../../../packages/vectors/src/types";
import TokenBucket from "./index";
import vectors from "./vectors.json";

describe("rate-limiter/token-bucket", () => {
  it("passes its vectors", () => {
    const result = runVectors((params) => new TokenBucket(params), vectors as VectorsFile);
    if (result.failures.length > 0) throw new Error(formatFailures(result));
    expect(result.assertionsRun).toBeGreaterThan(0);
  });

  it("defaults to 100 tokens a second with a burst of 100", () => {
    const limiter = new TokenBucket();
    expect(limiter.tokensOf(1, 0)).toBe(100);
    let admitted = 0;
    for (let i = 0; i < 200; i += 1) if (limiter.allow(1, 1, 0)) admitted += 1;
    expect(admitted).toBe(100);
  });

  it("starts a key it has never seen at a full balance", () => {
    // A bucket that started empty would refuse a first request for a reason the
    // caller could do nothing about: it has been idle for all of history.
    const limiter = new TokenBucket({ ratePerSec: 10, burst: 3 });
    expect(limiter.allow(42, 3, 5_000)).toBe(true);
    expect(limiter.allow(42, 1, 5_000)).toBe(false);
  });

  it("holds the long-run rate no matter how the requests arrive", () => {
    // Bursty, steady or clustered, a token bucket admits its rate plus at most
    // one burst over any long run. Three arrival patterns, one answer.
    for (const stride of [1, 7, 100]) {
      const limiter = new TokenBucket({ ratePerSec: 50, burst: 10 });
      let admitted = 0;
      for (let now = 0; now < 20_000; now += stride) {
        if (limiter.allow(1, 1, now)) admitted += 1;
      }
      const perSecond = admitted / 20;
      expect(perSecond).toBeGreaterThan(9);
      expect(perSecond).toBeLessThanOrEqual(51);
    }
  });

  it("never exceeds rate x time + burst over any interval", () => {
    // The guarantee a token bucket actually makes, checked directly rather
    // than at a chosen instant.
    const ratePerSec = 40;
    const burst = 7;
    const limiter = new TokenBucket({ ratePerSec, burst });
    const admitted: number[] = [];
    for (let now = 0; now < 10_000; now += 3) if (limiter.allow(1, 1, now)) admitted.push(now);

    for (let i = 0; i < admitted.length; i += 1) {
      for (const span of [100, 1_000, 5_000]) {
        const start = admitted[i]!;
        const inSpan = admitted.filter((t) => t >= start && t < start + span).length;
        expect(inSpan).toBeLessThanOrEqual(Math.floor((ratePerSec * span) / 1_000) + burst);
      }
    }
  });

  it("discards the overflow rather than banking it", () => {
    const limiter = new TokenBucket({ ratePerSec: 100, burst: 5 });
    limiter.allow(1, 5, 0);
    // A day of idling is worth exactly one burst, not a day's worth of tokens.
    expect(limiter.tokensOf(1, 86_400_000)).toBe(5);
    expect(limiter.allow(1, 5, 86_400_000)).toBe(true);
    expect(limiter.allow(1, 1, 86_400_000)).toBe(false);
  });

  it("keeps three integers per key", () => {
    const limiter = new TokenBucket();
    for (let key = 0; key < 500; key += 1) limiter.allow(key, 1, 0);
    expect(limiter.stateSize()).toBe(500);
  });
});
