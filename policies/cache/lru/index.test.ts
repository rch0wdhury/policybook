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
import Lru from "./index";
import vectors from "./vectors.json";

describe("cache/lru", () => {
  it("passes its vectors", () => {
    const result = runVectors((params) => new Lru(params), vectors as VectorsFile);
    if (result.failures.length > 0) throw new Error(formatFailures(result));
    expect(result.assertionsRun).toBeGreaterThan(0);
  });

  it("rejects a capacity that is not a positive integer", () => {
    expect(() => new Lru({ capacity: 0 })).toThrow(RangeError);
    expect(() => new Lru({ capacity: 2.5 })).toThrow(RangeError);
  });

  it("explains an evict on an empty cache", () => {
    expect(() => new Lru<string>({ capacity: 2 }).evict()).toThrow(/nothing resident/);
  });

  it("explains a hit for a key it does not hold", () => {
    // A caller that reports hit=true for an absent key has a bug in its
    // residency tracking, and silently inserting would hide it.
    const policy = new Lru<string>({ capacity: 2 });
    expect(() => policy.onAccess("ghost", true)).toThrow(/does not hold/);
  });

  it("reuses slots, so a long run stays within its allocation", () => {
    const policy = new Lru<number>({ capacity: 4 });
    for (let key = 0; key < 10_000; key += 1) {
      policy.onAccess(key, false);
      if (policy.size() > 4) policy.evict();
    }
    expect(policy.size()).toBe(4);
  });

  it("beats FIFO on the canonical Zipf trace", () => {
    // The reason LRU is the default: keeping what was recently used is worth
    // real hit rate on skewed traffic.
    const spec = CACHE_TRACES["zipf-1.0-100k"]!;
    const trace = generateCacheTrace(spec.id);
    const options = { capacity: spec.capacity, keyUniverse: spec.keyUniverse };

    const lru = cacheMetrics(runCacheTrace(new Lru<number>({ capacity: spec.capacity }), trace, options));
    const fifo = cacheMetrics(
      runCacheTrace(new Fifo<number>({ capacity: spec.capacity }), trace, options),
    );

    expect(lru.hitRate).toBeGreaterThan(fifo.hitRate);
  });

  it("scores nothing on a loop one larger than the cache", () => {
    // LRU's pathological case: each key is evicted exactly before its next use.
    const capacity = 8;
    const policy = new Lru<number>({ capacity });
    const trace = new Uint32Array(1_000);
    for (let index = 0; index < trace.length; index += 1) trace[index] = index % (capacity + 1);

    const result = runCacheTrace(policy, trace, { capacity, keyUniverse: capacity + 1 });
    // Only the first pass can miss "for free"; after that every access misses.
    expect(result.hits).toBe(0);
  });

  it("loses its working set to a scan", () => {
    // LRU's defining failure, and the reason SIEVE, S3-FIFO, ARC and 2Q exist.
    const capacity = 4;
    const options = { capacity, keyUniverse: 100 };

    // Warm a working set of four keys, sweep eight keys seen once each, then
    // ask for the working set again. One trace, because the harness owns
    // residency and a policy cannot be carried across separate runs.
    const withScan = Uint32Array.from([
      1, 2, 3, 4, 1, 2, 3, 4, 10, 11, 12, 13, 14, 15, 16, 17, 1, 2, 3, 4,
    ]);
    const withoutScan = Uint32Array.from([1, 2, 3, 4, 1, 2, 3, 4, 1, 2, 3, 4]);

    const scanned = runCacheTrace(new Lru<number>({ capacity }), withScan, options);
    const clean = runCacheTrace(new Lru<number>({ capacity }), withoutScan, options);

    // Without the scan, the working set stays cached: 8 of 12 accesses hit.
    expect(clean.hits).toBe(8);
    // With it, the last four all miss — the scan evicted everything, and the
    // only hits left are the ones from before it arrived.
    expect(scanned.hits).toBe(4);
  });
});
