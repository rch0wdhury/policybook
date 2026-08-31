import { describe, expect, it } from "vitest";
import { Rng } from "../../../packages/core/src/rng";
import { formatFailures, runVectors } from "../../../packages/vectors/src/run";
import type { VectorsFile } from "../../../packages/vectors/src/types";
import SlidingLog from "./index";
import vectors from "./vectors.json";

describe("rate-limiter/sliding-log", () => {
  it("passes its vectors", () => {
    const result = runVectors((params) => new SlidingLog(params), vectors as VectorsFile);
    if (result.failures.length > 0) throw new Error(formatFailures(result));
    expect(result.assertionsRun).toBeGreaterThan(0);
  });

  it("defaults to 100 permits a second", () => {
    const limiter = new SlidingLog();
    let admitted = 0;
    for (let i = 0; i < 200; i += 1) if (limiter.allow(1, 1, 0)) admitted += 1;
    expect(admitted).toBe(100);
  });

  it("enforces the limit over every window, not just aligned ones", () => {
    // This is the claim that distinguishes it, so it is checked exhaustively
    // rather than at a chosen instant: over a long random arrival stream, no
    // window of any offset ever contains more than `limit` admitted requests.
    const limit = 4;
    const windowMs = 100;
    const limiter = new SlidingLog({ limit, windowMs });
    const rng = new Rng(11);
    const admitted: number[] = [];

    let now = 0;
    for (let step = 0; step < 4_000; step += 1) {
      now += rng.nextInt(40);
      if (limiter.allow(1, 1, now)) admitted.push(now);
    }
    expect(admitted.length).toBeGreaterThan(100);

    // Every window that could be maximal starts at an admitted timestamp.
    for (const start of admitted) {
      const inWindow = admitted.filter((t) => t > start - windowMs && t <= start).length;
      expect(inWindow).toBeLessThanOrEqual(limit);
    }
  });

  it("expires an entry exactly windowMs after it arrived", () => {
    const limiter = new SlidingLog({ limit: 1, windowMs: 1_000 });
    limiter.allow(1, 1, 0);
    expect(limiter.countOf(1, 999)).toBe(1);
    expect(limiter.countOf(1, 1_000)).toBe(0);
  });

  it("reuses ring slots without losing the order of what is live", () => {
    // Five cycles through a ring of three: if head or count arithmetic drifted
    // by one, the oldest entry would be misidentified and the limit would leak.
    const limiter = new SlidingLog({ limit: 3, windowMs: 100 });
    for (let cycle = 0; cycle < 5; cycle += 1) {
      const base = cycle * 100;
      expect(limiter.allow(1, 3, base)).toBe(true);
      expect(limiter.allow(1, 1, base + 50)).toBe(false);
      expect(limiter.retryAfter(1, base + 50)).toBe(50);
      expect(limiter.countOf(1, base + 99)).toBe(3);
      expect(limiter.countOf(1, base + 100)).toBe(0);
    }
  });

  it("charges a costly request several slots, which age out together", () => {
    const limiter = new SlidingLog({ limit: 5, windowMs: 1_000 });
    expect(limiter.allow(1, 3, 100)).toBe(true);
    expect(limiter.countOf(1, 100)).toBe(3);
    expect(limiter.countOf(1, 1_099)).toBe(3);
    expect(limiter.countOf(1, 1_100)).toBe(0);
  });

  it("costs limit timestamps per key, which is the reason not to use it", () => {
    const limiter = new SlidingLog({ limit: 100, windowMs: 1_000 });
    for (let key = 0; key < 200; key += 1) limiter.allow(key, 1, 0);
    // 200 keys x 100 timestamps, held whether or not they are ever used again.
    expect(limiter.stateSize()).toBe(200);
  });
});
