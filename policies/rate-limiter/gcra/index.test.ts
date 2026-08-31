import { describe, expect, it } from "vitest";
import { formatFailures, runVectors } from "../../../packages/vectors/src/run";
import type { VectorsFile } from "../../../packages/vectors/src/types";
import Gcra from "./index";
import vectors from "./vectors.json";

describe("rate-limiter/gcra", () => {
  it("passes its vectors", () => {
    const result = runVectors((params) => new Gcra(params), vectors as VectorsFile);
    if (result.failures.length > 0) throw new Error(formatFailures(result));
    expect(result.assertionsRun).toBeGreaterThan(0);
  });

  it("rejects parameters that are not positive integers", () => {
    expect(() => new Gcra({ ratePerSec: 0 })).toThrow(RangeError);
    expect(() => new Gcra({ ratePerSec: 1.5 })).toThrow(RangeError);
    expect(() => new Gcra({ burst: 0 })).toThrow(RangeError);
    expect(() => new Gcra({ burst: -1 })).toThrow(RangeError);
  });

  it("defaults to 100 permits a second with a burst of 100", () => {
    const limiter = new Gcra();
    expect(limiter.tokensOf(1, 0)).toBe(100);
    let admitted = 0;
    for (let i = 0; i < 200; i += 1) if (limiter.allow(1, 1, 0)) admitted += 1;
    expect(admitted).toBe(100);
  });

  it("enforces exact spacing at a burst of one", () => {
    // With no tolerance the TAT *is* the next admissible instant, which is
    // GCRA at its most literal: one permit every emission interval.
    const limiter = new Gcra({ ratePerSec: 50, burst: 1 });
    const admitted: number[] = [];
    for (let now = 0; now < 200; now += 1) if (limiter.allow(1, 1, now)) admitted.push(now);
    expect(admitted).toEqual([0, 20, 40, 60, 80, 100, 120, 140, 160, 180]);
  });

  it("keeps one integer per key, which is the reason to choose it", () => {
    const limiter = new Gcra();
    for (let key = 0; key < 500; key += 1) limiter.allow(key, 1, 0);
    expect(limiter.stateSize()).toBe(500);
  });

  it("does not let an idle key bank more than its burst", () => {
    // The `max(now, tat)` in the update. Without it a TAT left far in the past
    // would accumulate unbounded credit, and a key silent for a day could
    // spend a day's worth of permits at once.
    const limiter = new Gcra({ ratePerSec: 100, burst: 5 });
    limiter.allow(1, 5, 0);
    expect(limiter.tokensOf(1, 86_400_000)).toBe(5);
    expect(limiter.allow(1, 5, 86_400_000)).toBe(true);
    expect(limiter.allow(1, 1, 86_400_000)).toBe(false);
  });

  it("never exceeds rate x time + burst over any interval", () => {
    const ratePerSec = 40;
    const burst = 7;
    const limiter = new Gcra({ ratePerSec, burst });
    const admitted: number[] = [];
    for (let now = 0; now < 10_000; now += 3) if (limiter.allow(1, 1, now)) admitted.push(now);

    for (const start of admitted) {
      for (const span of [100, 1_000, 5_000]) {
        const inSpan = admitted.filter((t) => t >= start && t < start + span).length;
        expect(inSpan).toBeLessThanOrEqual(Math.floor((ratePerSec * span) / 1_000) + burst);
      }
    }
  });

  it("reports a wait that admits when it elapses", () => {
    const limiter = new Gcra({ ratePerSec: 7, burst: 3 });
    let now = 0;
    let denials = 0;
    for (let step = 0; step < 300; step += 1) {
      now += step % 23;
      if (limiter.allow(1, 1, now)) continue;
      denials += 1;
      const wait = limiter.retryAfter(1, now);
      expect(limiter.allow(1, 1, now + wait)).toBe(true);
      now += wait;
    }
    expect(denials).toBeGreaterThan(20);
  });
});
