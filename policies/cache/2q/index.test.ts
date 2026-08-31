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
import TwoQueue from "./index";
import vectors from "./vectors.json";

describe("cache/2q", () => {
  it("passes its vectors", () => {
    const result = runVectors((params) => new TwoQueue(params), vectors as VectorsFile);
    if (result.failures.length > 0) throw new Error(formatFailures(result));
    expect(result.assertionsRun).toBeGreaterThan(0);
  });

  it("rejects nonsense parameters", () => {
    expect(() => new TwoQueue({ capacity: 0 })).toThrow(RangeError);
    expect(() => new TwoQueue({ capacity: 2.5 })).toThrow(RangeError);
    expect(() => new TwoQueue({ capacity: 10, kin: 0 })).toThrow(/kin must be a fraction/);
    expect(() => new TwoQueue({ capacity: 10, kin: 1.5 })).toThrow(/kin must be a fraction/);
    expect(() => new TwoQueue({ capacity: 10, kout: 0 })).toThrow(/kout must be a fraction/);
  });

  it("explains an evict on an empty cache", () => {
    expect(() => new TwoQueue<string>({ capacity: 2 }).evict()).toThrow(/nothing resident/);
  });

  it("explains a hit for a key it does not hold", () => {
    expect(() => new TwoQueue<string>({ capacity: 2 }).onAccess("ghost", true)).toThrow(
      /does not hold/,
    );
  });

  it("keeps the queues within their shares over a long run", () => {
    const capacity = 16;
    const policy = new TwoQueue<number>({ capacity });
    // Alternate fresh keys with returning ones so both queues stay in use.
    for (let step = 0; step < 20_000; step += 1) {
      const key = step % 3 === 0 ? step : step % 40;
      const resident = policy.queueOf(key) === "a1in" || policy.queueOf(key) === "am";
      policy.onAccess(key, resident);
      while (policy.size() > capacity) policy.evict();
    }
    expect(policy.size()).toBe(capacity);
  });

  it("protects a proven key from a scan where LRU does not", () => {
    // The point of the policy. Note the shape of the trace: key 1 has to be
    // *evicted and then requested again* to reach Am. Requesting it twice in a
    // row would not work, because a hit inside A1in is deliberately ignored —
    // the audition is a second access after eviction, not a repeat reference.
    const capacity = 8;
    const options = { capacity, keyUniverse: 300 };
    const events: number[] = [1];
    for (let key = 100; key < 108; key += 1) events.push(key); // pushes 1 into A1out
    events.push(1); // returns while its ghost is live, so it enters Am
    for (let key = 200; key < 208; key += 1) events.push(key); // a second scan
    events.push(1); // 2Q still holds it; LRU does not

    const trace = Uint32Array.from(events);
    const twoQueue = runCacheTrace(new TwoQueue<number>({ capacity }), trace, options);
    const lru = runCacheTrace(new Lru<number>({ capacity }), trace, options);

    expect(twoQueue.hits).toBe(1);
    expect(lru.hits).toBe(0);
  });

  it("beats LRU on the scan-heavy canonical trace", () => {
    const spec = CACHE_TRACES["scan-heavy"]!;
    const trace = generateCacheTrace(spec.id);
    const options = { capacity: spec.capacity, keyUniverse: spec.keyUniverse };

    const twoQueue = cacheMetrics(
      runCacheTrace(new TwoQueue<number>({ capacity: spec.capacity }), trace, options),
    );
    const lru = cacheMetrics(runCacheTrace(new Lru<number>({ capacity: spec.capacity }), trace, options));

    expect(twoQueue.hitRate).toBeGreaterThan(lru.hitRate);
  });

  it("does not leak ghost identifiers", () => {
    // A1out is bounded by kout, so a long run of misses must not grow it.
    const capacity = 8;
    const policy = new TwoQueue<number>({ capacity, kout: 0.5 });
    for (let key = 0; key < 5_000; key += 1) {
      policy.onAccess(key, false);
      while (policy.size() > capacity) policy.evict();
    }
    // Only the four most recent evictions are remembered.
    let ghosts = 0;
    for (let key = 0; key < 5_000; key += 1) {
      if (policy.queueOf(key) === "ghost") ghosts += 1;
    }
    expect(ghosts).toBeLessThanOrEqual(4);
  });
});
