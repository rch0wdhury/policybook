import { describe, expect, it } from "vitest";
import {
  CACHE_TRACES,
  cacheMetrics,
  generateCacheTrace,
  runCacheTrace,
} from "../../../packages/core/src/domains/cache";
import { formatFailures, runVectors } from "../../../packages/vectors/src/run";
import type { VectorsFile } from "../../../packages/vectors/src/types";
import Lfu from "../lfu/index";
import Lru from "../lru/index";
import WTinyLfu from "./index";
import vectors from "./vectors.json";

describe("cache/w-tinylfu", () => {
  it("passes its vectors", () => {
    const result = runVectors((params) => new WTinyLfu(params), vectors as VectorsFile);
    if (result.failures.length > 0) throw new Error(formatFailures(result));
    expect(result.assertionsRun).toBeGreaterThan(0);
  });

  it("rejects nonsense parameters", () => {
    // Capacity 1 has no room for both a window and a main cache.
    expect(() => new WTinyLfu({ capacity: 1 })).toThrow(/at least 2/);
    expect(() => new WTinyLfu({ capacity: 2.5 })).toThrow(RangeError);
    expect(() => new WTinyLfu({ capacity: 10, windowFraction: 0 })).toThrow(/windowFraction/);
    expect(() => new WTinyLfu({ capacity: 10, windowFraction: 1 })).toThrow(/windowFraction/);
    expect(() => new WTinyLfu({ capacity: 10, protectedFraction: 1 })).toThrow(
      /protectedFraction/,
    );
  });

  it("explains an evict on an empty cache", () => {
    expect(() => new WTinyLfu({ capacity: 4 }).evict()).toThrow(/nothing resident/);
  });

  it("explains a hit for a key it does not hold", () => {
    expect(() => new WTinyLfu({ capacity: 4 }).onAccess(99, true)).toThrow(/does not hold/);
  });

  it("saturates counters rather than overflowing them", () => {
    // Four-bit counters stop at 15. A key hammered a million times must not
    // wrap around to zero and suddenly look unpopular.
    const policy = new WTinyLfu({ capacity: 64 });
    for (let repeat = 0; repeat < 5_000; repeat += 1) {
      policy.onAccess(1, policy.segmentOf(1) !== "absent");
      while (policy.size() > 64) policy.evict();
    }
    const frequency = policy.frequencyOf(1);
    expect(frequency).toBeGreaterThan(0);
    expect(frequency).toBeLessThanOrEqual(16); // 15 counters plus the doorkeeper bit
  });

  it("ages its estimates instead of accumulating forever", () => {
    // The halving is what separates W-TinyLFU from LFU. Without it, a key that
    // was popular long ago would hold its estimate indefinitely.
    const capacity = 8;
    const policy = new WTinyLfu({ capacity });

    for (let repeat = 0; repeat < 12; repeat += 1) {
      policy.onAccess(1, policy.segmentOf(1) !== "absent");
      while (policy.size() > capacity) policy.evict();
    }
    const before = policy.frequencyOf(1);
    expect(before).toBeGreaterThan(4);

    // Drive plenty of unrelated traffic, which triggers the periodic halving.
    for (let key = 100; key < 400; key += 1) {
      policy.onAccess(key, false);
      while (policy.size() > capacity) policy.evict();
    }
    expect(policy.frequencyOf(1)).toBeLessThan(before);
  });

  it("beats LRU on the canonical Zipf trace", () => {
    const spec = CACHE_TRACES["zipf-1.0-100k"]!;
    const trace = generateCacheTrace(spec.id);
    const options = { capacity: spec.capacity, keyUniverse: spec.keyUniverse };

    const tiny = cacheMetrics(runCacheTrace(new WTinyLfu({ capacity: spec.capacity }), trace, options));
    const lru = cacheMetrics(runCacheTrace(new Lru<number>({ capacity: spec.capacity }), trace, options));

    // The headline claim: admission control is worth several points of hit rate.
    expect(tiny.hitRate).toBeGreaterThan(lru.hitRate + 0.03);
  });

  it("beats LFU when popularity shifts", () => {
    // Aging is the difference. LFU holds yesterday's winners; W-TinyLFU halves
    // its counters and follows the workload.
    const spec = CACHE_TRACES["shifting-popularity"]!;
    const trace = generateCacheTrace(spec.id);
    const options = { capacity: spec.capacity, keyUniverse: spec.keyUniverse };

    const tiny = cacheMetrics(runCacheTrace(new WTinyLfu({ capacity: spec.capacity }), trace, options));
    const lfu = cacheMetrics(runCacheTrace(new Lfu<number>({ capacity: spec.capacity }), trace, options));

    expect(tiny.hitRate).toBeGreaterThan(lfu.hitRate);
  });

  it("resists scans", () => {
    const spec = CACHE_TRACES["scan-heavy"]!;
    const trace = generateCacheTrace(spec.id);
    const options = { capacity: spec.capacity, keyUniverse: spec.keyUniverse };

    const tiny = cacheMetrics(runCacheTrace(new WTinyLfu({ capacity: spec.capacity }), trace, options));
    const lru = cacheMetrics(runCacheTrace(new Lru<number>({ capacity: spec.capacity }), trace, options));

    expect(tiny.hitRate).toBeGreaterThan(lru.hitRate);
  });

  it("keeps the window at its share once the cache is warm", () => {
    const capacity = 100;
    const policy = new WTinyLfu({ capacity });
    for (let key = 0; key < 5_000; key += 1) {
      policy.onAccess(key, false);
      while (policy.size() > capacity) policy.evict();
    }

    let windowed = 0;
    for (let key = 0; key < 5_000; key += 1) {
      if (policy.segmentOf(key) === "window") windowed += 1;
    }
    // windowFraction 0.01 of 100 is exactly one entry.
    expect(windowed).toBe(1);
    expect(policy.size()).toBe(capacity);
  });
});
