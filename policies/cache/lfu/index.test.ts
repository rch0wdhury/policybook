import { describe, expect, it } from "vitest";
import {
  CACHE_TRACES,
  cacheMetrics,
  generateCacheTrace,
  runCacheTrace,
} from "../../../packages/core/src/domains/cache";
import { formatFailures, runVectors } from "../../../packages/vectors/src/run";
import type { VectorsFile } from "../../../packages/vectors/src/types";
import Lru from "../lru/index";
import Lfu from "./index";
import vectors from "./vectors.json";

describe("cache/lfu", () => {
  it("passes its vectors", () => {
    const result = runVectors((params) => new Lfu(params), vectors as VectorsFile);
    if (result.failures.length > 0) throw new Error(formatFailures(result));
    expect(result.assertionsRun).toBeGreaterThan(0);
  });

  it("rejects a capacity that is not a positive integer", () => {
    expect(() => new Lfu({ capacity: 0 })).toThrow(RangeError);
    expect(() => new Lfu({ capacity: 2.5 })).toThrow(RangeError);
  });

  it("explains an evict on an empty cache", () => {
    expect(() => new Lfu<string>({ capacity: 2 }).evict()).toThrow(/nothing resident/);
  });

  it("explains a hit for a key it does not hold", () => {
    const policy = new Lfu<string>({ capacity: 2 });
    expect(() => policy.onAccess("ghost", true)).toThrow(/does not hold/);
  });

  it("counts frequency exactly", () => {
    const policy = new Lfu<string>({ capacity: 4 });
    policy.onAccess("a", false);
    for (let repeat = 0; repeat < 9; repeat += 1) policy.onAccess("a", true);
    expect(policy.frequencyOf("a")).toBe(10);
    expect(policy.frequencyOf("absent")).toBe(0);
  });

  it("does not exhaust its frequency classes on a long run", () => {
    // Classes are pooled, so one must be released whenever it empties. A leak
    // would surface here rather than in production.
    const policy = new Lfu<number>({ capacity: 4 });
    for (let round = 0; round < 5_000; round += 1) {
      const key = round % 6;
      const resident = policy.frequencyOf(key) > 0;
      policy.onAccess(key, resident);
      if (policy.size() > 4) policy.evict();
    }
    expect(policy.size()).toBe(4);
  });

  it("keeps a hot key through a scan", () => {
    // LFU's accidental virtue: scan keys are touched once and never accumulate
    // enough frequency to displace the working set. Where LRU would evict the
    // hot key as the least recently used, LFU keeps it.
    const capacity = 4;
    const options = { capacity, keyUniverse: 100 };
    const trace = Uint32Array.from([
      1, 1, 1, 1, 1, 1, 10, 11, 12, 13, 14, 15, 16, 17, 1,
    ]);

    const lfu = runCacheTrace(new Lfu<number>({ capacity }), trace, options);
    const lru = runCacheTrace(new Lru<number>({ capacity }), trace, options);

    // Five hits warming the key, then the final access hits too: LFU still has it.
    expect(lfu.hits).toBe(6);
    // LRU evicted it during the scan, so its final access misses.
    expect(lru.hits).toBe(5);
  });

  it("holds stale winners when popularity shifts", () => {
    // LFU's defining failure. On the shifting trace it should do worse relative
    // to LRU than it does on the stable one, because it cannot forget.
    const options = (spec: { capacity: number; keyUniverse: number }) => ({
      capacity: spec.capacity,
      keyUniverse: spec.keyUniverse,
    });

    const stable = CACHE_TRACES["zipf-1.0-100k"]!;
    const stableTrace = generateCacheTrace(stable.id);
    const stableLfu = cacheMetrics(
      runCacheTrace(new Lfu<number>({ capacity: stable.capacity }), stableTrace, options(stable)),
    );
    const stableLru = cacheMetrics(
      runCacheTrace(new Lru<number>({ capacity: stable.capacity }), stableTrace, options(stable)),
    );

    const shifting = CACHE_TRACES["shifting-popularity"]!;
    const shiftingTrace = generateCacheTrace(shifting.id);
    const shiftingLfu = cacheMetrics(
      runCacheTrace(
        new Lfu<number>({ capacity: shifting.capacity }),
        shiftingTrace,
        options(shifting),
      ),
    );
    const shiftingLru = cacheMetrics(
      runCacheTrace(
        new Lru<number>({ capacity: shifting.capacity }),
        shiftingTrace,
        options(shifting),
      ),
    );

    // LFU's standing against LRU is strictly worse once popularity moves.
    expect(shiftingLfu.hitRate - shiftingLru.hitRate).toBeLessThan(
      stableLfu.hitRate - stableLru.hitRate,
    );
  });
});
