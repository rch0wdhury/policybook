import { describe, expect, it } from "vitest";
import { formatFailures, runVectors } from "../../../packages/vectors/src/run";
import type { VectorsFile } from "../../../packages/vectors/src/types";
import LeakyBucket from "./index";
import vectors from "./vectors.json";

describe("rate-limiter/leaky-bucket", () => {
  it("passes its vectors", () => {
    const result = runVectors((params) => new LeakyBucket(params), vectors as VectorsFile);
    if (result.failures.length > 0) throw new Error(formatFailures(result));
    expect(result.assertionsRun).toBeGreaterThan(0);
  });

  it("defaults to draining 100 a second with room for one", () => {
    const limiter = new LeakyBucket();
    expect(limiter.levelOf(1, 0)).toBe(0);
    let admitted = 0;
    for (let i = 0; i < 200; i += 1) if (limiter.allow(1, 1, 0)) admitted += 1;
    expect(admitted).toBe(1);
  });

  it("starts a key it has never seen empty", () => {
    const limiter = new LeakyBucket({ ratePerSec: 10, capacity: 3 });
    expect(limiter.allow(42, 3, 5_000)).toBe(true);
    expect(limiter.allow(42, 1, 5_000)).toBe(false);
  });

  it("smooths a burst into evenly spaced admissions", () => {
    // The behaviour the policy exists for. Two hundred requests arriving at
    // once leave one at a time, one every 1000/ratePerSec milliseconds.
    const limiter = new LeakyBucket({ ratePerSec: 25, capacity: 1 });
    const admitted: number[] = [];
    for (let now = 0; now < 500; now += 1) if (limiter.allow(1, 1, now)) admitted.push(now);

    expect(admitted[0]).toBe(0);
    for (let i = 1; i < admitted.length; i += 1) {
      // 25 a second is one every 40 ms, exactly, with no drift over the run.
      expect(admitted[i]! - admitted[i - 1]!).toBe(40);
    }
    expect(admitted.length).toBe(13);
  });

  it("lets a larger capacity absorb a burst again", () => {
    // Capacity is the whole difference between smoothing and permitting a
    // burst — raise it and the policy behaves as a token bucket does, because
    // it is one.
    const limiter = new LeakyBucket({ ratePerSec: 25, capacity: 10 });
    let admitted = 0;
    for (let i = 0; i < 50; i += 1) if (limiter.allow(1, 1, 0)) admitted += 1;
    expect(admitted).toBe(10);
  });

  it("floors the level at zero rather than going negative", () => {
    const limiter = new LeakyBucket({ ratePerSec: 100, capacity: 5 });
    limiter.allow(1, 1, 0);
    // Far more drain time than there is level to drain.
    expect(limiter.levelOf(1, 86_400_000)).toBe(0);
    expect(limiter.allow(1, 5, 86_400_000)).toBe(true);
    expect(limiter.allow(1, 1, 86_400_000)).toBe(false);
  });

  it("never exceeds rate x time + capacity over any interval", () => {
    const ratePerSec = 40;
    const capacity = 7;
    const limiter = new LeakyBucket({ ratePerSec, capacity });
    const admitted: number[] = [];
    for (let now = 0; now < 10_000; now += 3) if (limiter.allow(1, 1, now)) admitted.push(now);

    for (let i = 0; i < admitted.length; i += 1) {
      for (const span of [100, 1_000, 5_000]) {
        const start = admitted[i]!;
        const inSpan = admitted.filter((t) => t >= start && t < start + span).length;
        expect(inSpan).toBeLessThanOrEqual(Math.floor((ratePerSec * span) / 1_000) + capacity);
      }
    }
  });

  it("keeps three integers per key", () => {
    const limiter = new LeakyBucket();
    for (let key = 0; key < 500; key += 1) limiter.allow(key, 1, 0);
    expect(limiter.stateSize()).toBe(500);
  });
});
