/**
 * Does the runner agree with the benchmarks?
 *
 * The steppers in `simulation.ts` reimplement the core harnesses' loops so a
 * runner can stop between events. That duplication is only safe if the two
 * produce the same numbers, so these run each stepper to completion and compare
 * against the harness itself — which is the guarantee asks
 * for, made at the level where it can actually be checked.
 *
 * And step-back has to be exact rather than approximately right, so a property
 * test seeks to a hundred random steps in a random order and asserts the state
 * is identical to having walked there forwards.
 */

import { describe, expect, it } from "vitest";
import {
  CACHE_TRACES,
  cacheMetrics,
  generateCacheTrace,
  runCacheTrace,
} from "../../../../packages/core/src/domains/cache";
import {
  KV_CACHE_TRACES,
  kvCacheMetrics,
  runKvCacheTrace,
} from "../../../../packages/core/src/domains/kv-cache";
import {
  RATE_LIMITER_TRACES,
  generateRateLimiterTrace,
  rateLimiterMetrics,
  runRateLimiterTrace,
} from "../../../../packages/core/src/domains/rate-limiter";
import Sieve from "../../../../policies/cache/sieve/index";
import Lru from "../../../../policies/cache/lru/index";
import TokenBucket from "../../../../policies/rate-limiter/token-bucket/index";
import StreamingLlm from "../../../../policies/kv-cache/streaming-llm/index";
import H2o from "../../../../policies/kv-cache/h2o/index";
import { createSimulation } from "./simulation";

const LENGTH = 5_000;

describe("the runner agrees with the core harness", () => {
  it("reaches the same cache metrics, event for event", () => {
    const spec = CACHE_TRACES["zipf-1.0-100k"]!;
    const trace = generateCacheTrace("zipf-1.0-100k", LENGTH);

    const expected = cacheMetrics(
      runCacheTrace(new Sieve({ capacity: spec.capacity }), trace, {
        capacity: spec.capacity,
        keyUniverse: spec.keyUniverse,
      }),
    );

    const simulation = createSimulation(
      "cache",
      (params) => new Sieve(params as { capacity: number }),
      { capacity: spec.capacity },
      "zipf-1.0-100k",
      LENGTH,
    );
    simulation.seek(simulation.totalSteps);

    expect(simulation.frame().metrics).toEqual(expected);
  });

  it("reaches the same cache metrics for a second policy", () => {
    // One policy agreeing could be luck in the loop's shape; two is the loop.
    const spec = CACHE_TRACES["zipf-1.0-100k"]!;
    const trace = generateCacheTrace("zipf-1.0-100k", LENGTH);

    const expected = cacheMetrics(
      runCacheTrace(new Lru({ capacity: spec.capacity }), trace, {
        capacity: spec.capacity,
        keyUniverse: spec.keyUniverse,
      }),
    );

    const simulation = createSimulation(
      "cache",
      (params) => new Lru(params as { capacity: number }),
      { capacity: spec.capacity },
      "zipf-1.0-100k",
      LENGTH,
    );
    simulation.seek(simulation.totalSteps);

    expect(simulation.frame().metrics).toEqual(expected);
  });

  it("reaches the same rate-limiter metrics", () => {
    const spec = RATE_LIMITER_TRACES["bursty"]!;
    const trace = generateRateLimiterTrace("bursty", LENGTH);
    const params = { ratePerSec: 100, burst: 100, maxKeys: 1024 };

    const expected = rateLimiterMetrics(
      runRateLimiterTrace(new TokenBucket(params), trace, { keyUniverse: spec.keyUniverse }),
    );

    const simulation = createSimulation(
      "rate-limiter",
      (p) => new TokenBucket(p as typeof params),
      params,
      "bursty",
      LENGTH,
    );
    simulation.seek(simulation.totalSteps);

    const actual = simulation.frame().metrics;
    // `jainFairness` is absent on single-key traces and the harness drops it;
    // comparing the keys that exist is the honest comparison.
    for (const [name, value] of Object.entries(expected)) {
      if (value === null || value === undefined) continue;
      expect(actual[name], name).toBe(value);
    }
  });

  it("reaches the same kv-cache metrics", () => {
    const spec = KV_CACHE_TRACES["decode-4096"]!;
    const budget = 256;

    const expected = kvCacheMetrics(
      runKvCacheTrace(new StreamingLlm({ budget }), spec, { budget, maxSteps: LENGTH }),
    );

    const simulation = createSimulation(
      "kv-cache",
      (params) => new StreamingLlm(params as { budget: number }),
      { budget },
      "decode-4096",
      LENGTH,
    );
    simulation.seek(simulation.totalSteps);

    const actual = simulation.frame().metrics;
    expect(actual["retainedAttentionMass"]).toBe(expected.retainedAttentionMass);
    expect(actual["heavyHitterRecall"]).toBe(expected.heavyHitterRecall);
  });

  it("agrees for an attention-reading policy too", () => {
    // StreamingLLM ignores the attention vector entirely, so it would still
    // agree if the runner passed nonsense. H2O reads every weight.
    const spec = KV_CACHE_TRACES["decode-4096"]!;
    const budget = 256;

    const expected = kvCacheMetrics(
      runKvCacheTrace(new H2o({ budget }), spec, { budget, maxSteps: LENGTH }),
    );

    const simulation = createSimulation(
      "kv-cache",
      (params) => new H2o(params as { budget: number }),
      { budget },
      "decode-4096",
      LENGTH,
    );
    simulation.seek(simulation.totalSteps);

    expect(simulation.frame().metrics["retainedAttentionMass"]).toBe(
      expected.retainedAttentionMass,
    );
  });
});

describe("seeking", () => {
  it("lands in the same state whether it walked forwards or jumped back", () => {
    // Step-back is a replay from the beginning, which is exact by construction
    // — but only if the reset actually resets everything. A field left behind
    // would show up here and nowhere else.
    const spec = CACHE_TRACES["zipf-1.0-100k"]!;
    const build = () =>
      createSimulation(
        "cache",
        (params) => new Sieve(params as { capacity: number }),
        { capacity: spec.capacity },
        "zipf-1.0-100k",
        1_000,
      );

    const forward = build();
    const jumping = build();

    // A hundred steps in a shuffled order, so the jumping simulation is
    // constantly going backwards while the reference walks forwards.
    const targets = Array.from({ length: 100 }, (_, i) => (i * 37) % 1_000);

    for (const target of targets) {
      forward.seek(0);
      forward.seek(target);
      jumping.seek(target);

      expect(jumping.frame(), `step ${target}`).toEqual(forward.frame());
    }
  });

  it("clamps out-of-range seeks instead of throwing", () => {
    const simulation = createSimulation(
      "cache",
      (params) => new Sieve(params as { capacity: number }),
      { capacity: 16 },
      "zipf-1.0-100k",
      100,
    );

    simulation.seek(-50);
    expect(simulation.step).toBe(0);
    simulation.seek(10_000);
    expect(simulation.step).toBe(simulation.totalSteps);
  });

  it("refuses a domain it has no runner for", () => {
    // retry is episodic rather than a stream of events, and pretending
    // otherwise would produce a runner whose numbers meant nothing.
    expect(() => createSimulation("retry", () => ({}), {}, "outage-30s")).toThrow(/no runner/);
  });
});
