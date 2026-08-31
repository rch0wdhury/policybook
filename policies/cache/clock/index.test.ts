import { describe, expect, it } from "vitest";
import {
  CACHE_TRACES,
  cacheMetrics,
  generateCacheTrace,
  runCacheTrace,
} from "../../../packages/core/src/domains/cache";
import { formatFailures, runVectors } from "../../../packages/vectors/src/run";
import type { VectorsFile } from "../../../packages/vectors/src/types";
import Fifo from "../fifo/index";
import Lru from "../lru/index";
import Clock from "./index";
import vectors from "./vectors.json";

describe("cache/clock", () => {
  it("passes its vectors", () => {
    const result = runVectors((params) => new Clock(params), vectors as VectorsFile);
    if (result.failures.length > 0) throw new Error(formatFailures(result));
    expect(result.assertionsRun).toBeGreaterThan(0);
  });

  it("rejects a capacity that is not a positive integer", () => {
    expect(() => new Clock({ capacity: 0 })).toThrow(RangeError);
    expect(() => new Clock({ capacity: 2.5 })).toThrow(RangeError);
  });

  it("explains an evict on an empty cache", () => {
    expect(() => new Clock<string>({ capacity: 2 }).evict()).toThrow(/nothing resident/);
  });

  it("explains a hit for a key it does not hold", () => {
    expect(() => new Clock<string>({ capacity: 2 }).onAccess("ghost", true)).toThrow(
      /does not hold/,
    );
  });

  it("terminates when every entry is referenced", () => {
    // The sweep clears bits as it goes, so a full lap always ends in an
    // eviction rather than spinning.
    const policy = new Clock<number>({ capacity: 4 });
    for (const key of [1, 2, 3, 4]) policy.onAccess(key, false);
    for (const key of [1, 2, 3, 4]) policy.onAccess(key, true);
    expect(policy.evict()).toBe(1);
    expect(policy.size()).toBe(3);
  });

  it("lands between FIFO and LRU on the canonical trace", () => {
    // CLOCK is an approximation of LRU built on FIFO, and its hit rate sits
    // where that description implies.
    const spec = CACHE_TRACES["zipf-1.0-100k"]!;
    const trace = generateCacheTrace(spec.id);
    const options = { capacity: spec.capacity, keyUniverse: spec.keyUniverse };

    const fifo = cacheMetrics(runCacheTrace(new Fifo<number>({ capacity: spec.capacity }), trace, options));
    const clock = cacheMetrics(runCacheTrace(new Clock<number>({ capacity: spec.capacity }), trace, options));
    const lru = cacheMetrics(runCacheTrace(new Lru<number>({ capacity: spec.capacity }), trace, options));

    expect(clock.hitRate).toBeGreaterThan(fifo.hitRate);
    // Close to LRU without matching it: that is the trade for a lock-free hit.
    expect(Math.abs(clock.hitRate - lru.hitRate)).toBeLessThan(0.05);
  });

  it("reuses slots over a long run", () => {
    const policy = new Clock<number>({ capacity: 8 });
    for (let key = 0; key < 20_000; key += 1) {
      policy.onAccess(key, false);
      if (policy.size() > 8) policy.evict();
    }
    expect(policy.size()).toBe(8);
  });
});
