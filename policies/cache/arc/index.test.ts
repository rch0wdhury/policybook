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
import Arc from "./index";
import vectors from "./vectors.json";

describe("cache/arc", () => {
  it("passes its vectors", () => {
    const result = runVectors((params) => new Arc(params), vectors as VectorsFile);
    if (result.failures.length > 0) throw new Error(formatFailures(result));
    expect(result.assertionsRun).toBeGreaterThan(0);
  });

  it("rejects a capacity that is not a positive integer", () => {
    expect(() => new Arc({ capacity: 0 })).toThrow(RangeError);
    expect(() => new Arc({ capacity: 2.5 })).toThrow(RangeError);
  });

  it("explains an evict that ARC did not schedule", () => {
    // ARC picks its victim while handling the miss, so an unprompted evict is a
    // contract violation rather than a request it can satisfy.
    expect(() => new Arc<string>({ capacity: 2 }).evict()).toThrow(/no replacement was scheduled/);
  });

  it("explains a hit for a key it does not hold", () => {
    expect(() => new Arc<string>({ capacity: 2 }).onAccess("ghost", true)).toThrow(
      /does not hold/,
    );
  });

  it("keeps the target within [0, capacity] under sustained pressure", () => {
    const capacity = 16;
    const policy = new Arc<number>({ capacity });
    for (let step = 0; step < 20_000; step += 1) {
      const key = step % 5 === 0 ? step : step % 48;
      const list = policy.listOfKey(key);
      policy.onAccess(key, list === "t1" || list === "t2");
      while (policy.size() > capacity) policy.evict();

      const target = policy.targetT1();
      expect(target).toBeGreaterThanOrEqual(0);
      expect(target).toBeLessThanOrEqual(capacity);
    }
    expect(policy.size()).toBe(capacity);
  });

  it("never tracks more than 2c keys", () => {
    // The ghost lists are what make ARC adaptive, and also what would make it
    // leak if they were unbounded.
    const capacity = 8;
    const policy = new Arc<number>({ capacity });
    for (let key = 0; key < 5_000; key += 1) {
      policy.onAccess(key, false);
      while (policy.size() > capacity) policy.evict();
    }

    let tracked = 0;
    for (let key = 0; key < 5_000; key += 1) {
      if (policy.listOfKey(key) !== "absent") tracked += 1;
    }
    expect(tracked).toBeLessThanOrEqual(2 * capacity);
  });

  it("adapts its target in both directions", () => {
    // A hit in B1 grows the target and a hit in B2 shrinks it. An
    // implementation with these inverted still runs and still caches, so this
    // is worth asserting directly rather than inferring from a hit rate.
    const policy = new Arc<string>({ capacity: 4 });
    const put = (key: string): void => {
      policy.onAccess(key, false);
      while (policy.size() > 4) policy.evict();
    };

    for (const key of ["a", "b", "c", "d"]) put(key);
    policy.onAccess("a", true);
    policy.onAccess("b", true);

    put("e");
    expect(policy.listOfKey("c")).toBe("b1");
    expect(policy.targetT1()).toBe(0);

    put("c"); // returns from B1
    expect(policy.targetT1()).toBe(1);

    put("f");
    put("g");
    expect(policy.listOfKey("a")).toBe("b2");

    put("a"); // returns from B2
    expect(policy.targetT1()).toBe(0);
  });

  it("beats LRU on the shifting-popularity trace", () => {
    // The workload ARC exists for: what is popular changes, and a fixed policy
    // cannot follow it.
    const spec = CACHE_TRACES["shifting-popularity"]!;
    const trace = generateCacheTrace(spec.id);
    const options = { capacity: spec.capacity, keyUniverse: spec.keyUniverse };

    const arc = cacheMetrics(runCacheTrace(new Arc<number>({ capacity: spec.capacity }), trace, options));
    const lru = cacheMetrics(runCacheTrace(new Lru<number>({ capacity: spec.capacity }), trace, options));

    expect(arc.hitRate).toBeGreaterThan(lru.hitRate);
  });
});
