import { describe, expect, it } from "vitest";
import {
  CACHE_TRACES,
  cacheMetrics,
  generateCacheTrace,
  runCacheTrace,
} from "../../../packages/core/src/domains/cache";
import { formatFailures, runVectors } from "../../../packages/vectors/src/run";
import type { VectorsFile } from "../../../packages/vectors/src/types";
import Fifo from "./index";
import vectors from "./vectors.json";

describe("cache/fifo", () => {
  it("passes its vectors", () => {
    const result = runVectors((params) => new Fifo(params), vectors as VectorsFile);
    if (result.failures.length > 0) throw new Error(formatFailures(result));
    expect(result.assertionsRun).toBeGreaterThan(0);
  });

  it("rejects a capacity that is not a positive integer", () => {
    expect(() => new Fifo({ capacity: 0 })).toThrow(RangeError);
    expect(() => new Fifo({ capacity: -1 })).toThrow(RangeError);
    expect(() => new Fifo({ capacity: 1.5 })).toThrow(RangeError);
  });

  it("defaults to a capacity of 1000", () => {
    const policy = new Fifo<number>();
    for (let key = 0; key < 1000; key += 1) policy.onAccess(key, false);
    expect(policy.size()).toBe(1000);
  });

  it("explains an evict on an empty cache", () => {
    expect(() => new Fifo<string>({ capacity: 2 }).evict()).toThrow(/nothing resident/);
  });

  it("explains being overfilled without an evict", () => {
    const policy = new Fifo<number>({ capacity: 2 });
    policy.onAccess(1, false);
    policy.onAccess(2, false);
    policy.onAccess(3, false); // one over capacity is allowed
    expect(() => policy.onAccess(4, false)).toThrow(/without an evict/);
  });

  it("runs the canonical trace and caches something", () => {
    const spec = CACHE_TRACES["zipf-1.0-100k"]!;
    const trace = generateCacheTrace(spec.id);
    const policy = new Fifo<number>({ capacity: spec.capacity });
    const metrics = cacheMetrics(
      runCacheTrace(policy, trace, { capacity: spec.capacity, keyUniverse: spec.keyUniverse }),
    );

    // A wide band: this asserts the policy works at scale, not its exact score,
    // which is the benchmark's job.
    expect(metrics.hitRate).toBeGreaterThan(0.2);
    expect(metrics.hitRate).toBeLessThan(0.9);
    expect(metrics.evictions).toBeGreaterThan(0);
  });

  it("gains nothing from hits, unlike a recency policy", () => {
    // Re-accessing a resident key must leave eviction order untouched. This is
    // the property that separates FIFO from LRU, at trace scale rather than in
    // a single vector.
    const policy = new Fifo<number>({ capacity: 3 });
    for (const key of [1, 2, 3]) policy.onAccess(key, false);
    for (let repeat = 0; repeat < 50; repeat += 1) policy.onAccess(1, true);

    policy.onAccess(4, false);
    expect(policy.evict()).toBe(1);
  });
});
