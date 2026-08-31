import { describe, expect, it } from "vitest";
import { Rng } from "../../../packages/core/src/rng";
import { formatFailures, runVectors } from "../../../packages/vectors/src/run";
import type { VectorsFile } from "../../../packages/vectors/src/types";
import SlidingCounter from "./index";
import vectors from "./vectors.json";

describe("rate-limiter/sliding-counter", () => {
  it("passes its vectors", () => {
    const result = runVectors((params) => new SlidingCounter(params), vectors as VectorsFile);
    if (result.failures.length > 0) throw new Error(formatFailures(result));
    expect(result.assertionsRun).toBeGreaterThan(0);
  });

  it("defaults to 100 permits a second", () => {
    const limiter = new SlidingCounter();
    let admitted = 0;
    for (let i = 0; i < 200; i += 1) if (limiter.allow(1, 1, 0)) admitted += 1;
    expect(admitted).toBe(100);
  });

  it("fades the previous window out linearly", () => {
    const limiter = new SlidingCounter({ limit: 100, windowMs: 1_000 });
    limiter.allow(1, 100, 0);
    // A quarter, half and three quarters into the next window, that much of
    // the old count has gone.
    expect(limiter.estimateOf(1, 1_000)).toBe(100);
    expect(limiter.estimateOf(1, 1_250)).toBe(75);
    expect(limiter.estimateOf(1, 1_500)).toBe(50);
    expect(limiter.estimateOf(1, 1_750)).toBe(25);
    expect(limiter.estimateOf(1, 1_999)).toBe(0);
  });

  it("floors the weighting, so it errs low rather than high", () => {
    // 5 x 999/1000 is 4.995. Flooring calls it 4, which admits a request an
    // exact calculation would refuse. That is a decision, not a rounding
    // artefact, and it is what keeps the three ports identical.
    const limiter = new SlidingCounter({ limit: 5, windowMs: 1_000 });
    limiter.allow(1, 5, 0);
    expect(limiter.estimateOf(1, 1_001)).toBe(4);
    expect(limiter.allow(1, 1, 1_001)).toBe(true);
  });

  it("clears both counts after a full window of silence", () => {
    const limiter = new SlidingCounter({ limit: 5, windowMs: 1_000 });
    limiter.allow(1, 5, 0);
    // Window 1000 carries the count; window 2000 is two on, so nothing from
    // window 0 is still inside the trailing window.
    expect(limiter.estimateOf(1, 1_000)).toBe(5);
    expect(limiter.estimateOf(1, 2_000)).toBe(0);
    expect(limiter.allow(1, 5, 2_000)).toBe(true);
  });

  it("stays close to the limit on a steady stream", () => {
    // The estimate is an approximation, so the useful assertion is not that it
    // is exact but that it does not drift: over sixty seconds at twice the
    // limit, admissions should sit near the configured rate rather than above
    // it by a growing margin.
    const limiter = new SlidingCounter({ limit: 100, windowMs: 1_000 });
    let admitted = 0;
    for (let now = 0; now < 60_000; now += 5) if (limiter.allow(1, 1, now)) admitted += 1;

    const perSecond = admitted / 60;
    expect(perSecond).toBeGreaterThan(95);
    expect(perSecond).toBeLessThanOrEqual(100);
  });

  it("never admits more than the limit over an aligned window", () => {
    const limiter = new SlidingCounter({ limit: 7, windowMs: 250 });
    const rng = new Rng(3);
    const admitted: number[] = [];

    let now = 0;
    for (let step = 0; step < 4_000; step += 1) {
      now += rng.nextInt(60);
      if (limiter.allow(1, 1, now)) admitted.push(now);
    }
    expect(admitted.length).toBeGreaterThan(100);

    const perWindow = new Map<number, number>();
    for (const t of admitted) {
      const window = t - (t % 250);
      perWindow.set(window, (perWindow.get(window) ?? 0) + 1);
    }
    for (const count of perWindow.values()) expect(count).toBeLessThanOrEqual(7);
  });

  it("keeps three integers per key, not a log of timestamps", () => {
    const limiter = new SlidingCounter({ limit: 100, windowMs: 1_000 });
    for (let key = 0; key < 500; key += 1) limiter.allow(key, 1, 0);
    expect(limiter.stateSize()).toBe(500);
  });
});
