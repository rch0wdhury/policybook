import { describe, expect, it } from "vitest";
import {
  CACHE_TRACES,
  cacheMetrics,
  generateCacheTrace,
  runCacheTrace,
} from "../../../packages/core/src/domains/cache";
import { formatFailures, runVectors } from "../../../packages/vectors/src/run";
import type { VectorsFile } from "../../../packages/vectors/src/types";
import Arc from "../arc/index";
import Lru from "../lru/index";
import Sieve from "../sieve/index";
import WTinyLfu from "../w-tinylfu/index";
import Opt from "./index";
import vectors from "./vectors.json";

describe("cache/opt", () => {
  it("passes its vectors", () => {
    const result = runVectors((params) => new Opt(params), vectors as VectorsFile);
    if (result.failures.length > 0) throw new Error(formatFailures(result));
    expect(result.assertionsRun).toBeGreaterThan(0);
  });

  it("rejects a capacity that is not a positive integer", () => {
    expect(() => new Opt({ capacity: 0, future: [] })).toThrow(RangeError);
    expect(() => new Opt({ capacity: 2.5, future: [] })).toThrow(RangeError);
  });

  it("refuses a trace that does not match its future", () => {
    // A bound computed against the wrong trace is not a bound, so this fails
    // loudly rather than reporting a plausible number.
    const policy = new Opt<number>({ capacity: 2, future: [1, 2, 3] });
    policy.onAccess(1, false);
    expect(() => policy.onAccess(9, false)).toThrow(/does not match the supplied future/);
  });

  it("refuses to run past the end of its future", () => {
    const policy = new Opt<number>({ capacity: 2, future: [1] });
    policy.onAccess(1, false);
    expect(() => policy.onAccess(1, false)).toThrow(/beyond the end/);
  });

  it("explains an evict on an empty cache", () => {
    expect(() => new Opt<number>({ capacity: 2, future: [] }).evict()).toThrow(
      /nothing resident/,
    );
  });

  it("matches the classic reference string, allowing for bypass", () => {
    // The standard operating-systems treatment reports nine faults here. That
    // figure is for demand paging, where a referenced page must be brought in.
    // A cache may decline to admit, so refusing key 4 — which appears once and
    // never returns — is legal and better. See the README.
    const reference = [7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2, 1, 2, 0, 1, 7, 0, 1];
    const capacity = 3;
    const result = runCacheTrace(
      new Opt<number>({ capacity, future: reference }),
      Uint32Array.from(reference),
      { capacity, keyUniverse: 8 },
    );

    expect(result.misses).toBe(8);
    expect(result.hits).toBe(12);
  });

  it("is never beaten by any online policy", () => {
    // This is the property that makes OPT worth having, and a policy that
    // violated it would mean a bug in the harness, the trace or the policy.
    for (const id of ["zipf-1.0-100k", "scan-heavy", "shifting-popularity"]) {
      const spec = CACHE_TRACES[id]!;
      const trace = generateCacheTrace(spec.id);
      const options = { capacity: spec.capacity, keyUniverse: spec.keyUniverse };
      const future = Array.from(trace);

      const optimal = cacheMetrics(
        runCacheTrace(new Opt<number>({ capacity: spec.capacity, future }), trace, options),
      );

      const contenders = [
        cacheMetrics(runCacheTrace(new Lru<number>({ capacity: spec.capacity }), trace, options)),
        cacheMetrics(runCacheTrace(new Sieve<number>({ capacity: spec.capacity }), trace, options)),
        cacheMetrics(runCacheTrace(new Arc<number>({ capacity: spec.capacity }), trace, options)),
        cacheMetrics(runCacheTrace(new WTinyLfu({ capacity: spec.capacity }), trace, options)),
      ];

      for (const contender of contenders) {
        expect(contender.hitRate).toBeLessThanOrEqual(optimal.hitRate);
      }
    }
  });

  it("leaves a visible gap above the best online policy", () => {
    // If OPT were only fractionally ahead the bound would be uninteresting.
    // On the canonical Zipf trace there is real room left.
    const spec = CACHE_TRACES["zipf-1.0-100k"]!;
    const trace = generateCacheTrace(spec.id);
    const options = { capacity: spec.capacity, keyUniverse: spec.keyUniverse };

    const optimal = cacheMetrics(
      runCacheTrace(
        new Opt<number>({ capacity: spec.capacity, future: Array.from(trace) }),
        trace,
        options,
      ),
    );
    const best = cacheMetrics(
      runCacheTrace(new WTinyLfu({ capacity: spec.capacity }), trace, options),
    );

    expect(optimal.hitRate).toBeGreaterThan(best.hitRate);
    expect(optimal.hitRate).toBeGreaterThan(0.75);
  });
});
