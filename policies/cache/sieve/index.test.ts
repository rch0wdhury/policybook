import { describe, expect, it } from "vitest";
import {
  CACHE_TRACES,
  cacheMetrics,
  generateCacheTrace,
  runCacheTrace,
} from "../../../packages/core/src/domains/cache";
import { formatFailures, runVectors } from "../../../packages/vectors/src/run";
import type { VectorsFile } from "../../../packages/vectors/src/types";
import Clock from "../clock/index";
import Lru from "../lru/index";
import Sieve from "./index";
import vectors from "./vectors.json";

describe("cache/sieve", () => {
  it("passes its vectors", () => {
    const result = runVectors((params) => new Sieve(params), vectors as VectorsFile);
    if (result.failures.length > 0) throw new Error(formatFailures(result));
    expect(result.assertionsRun).toBeGreaterThan(0);
  });

  it("rejects a capacity that is not a positive integer", () => {
    expect(() => new Sieve({ capacity: 0 })).toThrow(RangeError);
    expect(() => new Sieve({ capacity: 2.5 })).toThrow(RangeError);
  });

  it("explains an evict on an empty cache", () => {
    expect(() => new Sieve<string>({ capacity: 2 }).evict()).toThrow(/nothing resident/);
  });

  it("explains a hit for a key it does not hold", () => {
    expect(() => new Sieve<string>({ capacity: 2 }).onAccess("ghost", true)).toThrow(
      /does not hold/,
    );
  });

  it("terminates when every entry is visited", () => {
    const policy = new Sieve<number>({ capacity: 4 });
    for (const key of [1, 2, 3, 4]) policy.onAccess(key, false);
    for (const key of [1, 2, 3, 4]) policy.onAccess(key, true);
    expect(policy.evict()).toBe(1);
    expect(policy.size()).toBe(3);
  });

  it("diverges from CLOCK once a survivor's position matters", () => {
    // Both spare a referenced entry, so they agree on the first eviction. They
    // part on the second: CLOCK moved the survivor to the back of its queue and
    // takes the oldest entry, while SIEVE left it in place and its retained
    // hand has already moved past it onto the newest entry.
    const run = (policy: Sieve<string> | Clock<string>): [string, string] => {
      policy.onAccess("a", false);
      policy.onAccess("b", false);
      policy.onAccess("a", true);
      policy.onAccess("c", false);
      const first = policy.evict();
      policy.onAccess("c", true);
      policy.onAccess("d", false);
      return [first, policy.evict()];
    };

    expect(run(new Sieve<string>({ capacity: 2 }))).toEqual(["b", "d"]);
    expect(run(new Clock<string>({ capacity: 2 }))).toEqual(["b", "a"]);
  });

  it("reclaims a newcomer rather than a warm working set", () => {
    // Quick demotion in its simplest form. With every resident entry visited,
    // the hand clears its way through them and reaches the entry that just
    // arrived — so the newcomer pays, not the working set. LRU instead evicts
    // its oldest entry, losing part of a set it is actively serving.
    //
    // CLOCK does the same thing here, because a full lap of bit-clearing also
    // ends on the newcomer. The two only part once a survivor's *position*
    // matters, which the divergence test above covers.
    const warm = (policy: Sieve<string> | Clock<string> | Lru<string>): string => {
      for (const key of ["a", "b", "c", "d"]) policy.onAccess(key, false);
      for (const key of ["a", "b", "c", "d"]) policy.onAccess(key, true);
      policy.onAccess("new", false);
      return policy.evict();
    };

    expect(warm(new Sieve<string>({ capacity: 4 }))).toBe("new");
    expect(warm(new Clock<string>({ capacity: 4 }))).toBe("new");
    expect(warm(new Lru<string>({ capacity: 4 }))).toBe("a");
  });

  it("beats LRU on the scan-heavy canonical trace", () => {
    // The headline claim of the paper, on the registry's scan trace.
    const spec = CACHE_TRACES["scan-heavy"]!;
    const trace = generateCacheTrace(spec.id);
    const options = { capacity: spec.capacity, keyUniverse: spec.keyUniverse };

    const sieve = cacheMetrics(runCacheTrace(new Sieve<number>({ capacity: spec.capacity }), trace, options));
    const lru = cacheMetrics(runCacheTrace(new Lru<number>({ capacity: spec.capacity }), trace, options));

    expect(sieve.hitRate).toBeGreaterThan(lru.hitRate);
  });

  it("reuses slots over a long run", () => {
    const policy = new Sieve<number>({ capacity: 8 });
    for (let key = 0; key < 20_000; key += 1) {
      policy.onAccess(key, false);
      if (policy.size() > 8) policy.evict();
    }
    expect(policy.size()).toBe(8);
  });
});
