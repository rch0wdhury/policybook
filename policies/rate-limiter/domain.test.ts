/**
 * Properties the rate-limiter domain's published numbers must satisfy.
 *
 * These read the committed `bench.json` files rather than recomputing, so they
 * check what the READMEs actually claim. A stale or wrong benchmark is exactly
 * as damaging as a wrong implementation — arguably worse, since a reader
 * choosing a policy sees only the table.
 */

import { readFileSync } from "node:fs";
import { join } from "node:path";
import { describe, expect, it } from "vitest";
import {
  RATE_LIMITER_TRACES,
  generateRateLimiterTrace,
  rateLimiterMetrics,
  runRateLimiterTrace,
} from "../../packages/core/src/domains/rate-limiter";
import { discoverPolicies, findRepoRoot } from "../../packages/vectors/src/discover";
import Gcra from "./gcra/index";
import SlidingLog from "./sliding-log/index";
import TokenBucket from "./token-bucket/index";

interface BenchFile {
  policy: string;
  coreVersion: string;
  traces: Record<string, { metrics: Record<string, number>; perf: { opsPerSec: number } }>;
}

const repoRoot = findRepoRoot();
const policies = discoverPolicies(repoRoot).filter((policy) => policy.domain === "rate-limiter");

const bench = new Map<string, BenchFile>();
for (const policy of policies) {
  bench.set(
    policy.name,
    JSON.parse(readFileSync(join(policy.dir, "bench.json"), "utf8")) as BenchFile,
  );
}

const TRACES = ["steady", "bursty", "many-keys", "overload"];

describe("the published rate-limiter benchmark", () => {
  it("covers every policy on every trace", () => {
    expect(policies.length).toBe(7);
    for (const [name, file] of bench) {
      expect(Object.keys(file.traces).sort(), name).toEqual([...TRACES].sort());
    }
  });

  it("is all stable, now that it has numbers", () => {
    // A `stable` status requires a bench.json (the catalog enforces it), and
    // every policy here has one — this is the assertion that the promotion in
    // T28 actually happened rather than being half-applied.
    for (const policy of policies) {
      expect(policy.meta.status, policy.id).toBe("stable");
      expect(policy.meta.ports.sort(), policy.id).toEqual(["c", "python", "ts"]);
    }
  });

  it("converges under sustained overload, which is the domain's headline", () => {
    // Rate times time is not a property any policy can improve on. Six of the
    // seven land within a hair of each other; the seventh is the dual bucket,
    // whose burst allowance is its whole per-minute quota by construction.
    const perSecond = [...bench.entries()].filter(([name]) => name !== "dual-bucket");
    const rates = perSecond.map(([, file]) => file.traces["overload"]!.metrics["acceptRate"]!);

    expect(rates.length).toBe(6);
    expect(Math.max(...rates) - Math.min(...rates)).toBeLessThan(0.01);

    // And the outlier really is an outlier, not noise.
    const dual = bench.get("dual-bucket")!.traces["overload"]!.metrics["acceptRate"]!;
    expect(dual).toBeGreaterThan(Math.max(...rates) * 1.5);
  });

  it("refuses nothing on `steady` from a bucket, and a little from a window", () => {
    // `steady` runs just under the limit, so a policy that refuses anything is
    // leaking capacity. The buckets do not; the window policies do, because an
    // unlucky cluster inside one window is over the limit even when the average
    // is not. Small, and the reason the trace exists.
    for (const name of ["token-bucket", "leaky-bucket", "gcra", "dual-bucket"]) {
      expect(bench.get(name)!.traces["steady"]!.metrics["acceptRate"], name).toBe(1);
    }
    for (const name of ["fixed-window", "sliding-counter", "sliding-log"]) {
      const rate = bench.get(name)!.traces["steady"]!.metrics["acceptRate"]!;
      expect(rate, name).toBeLessThan(1);
      expect(rate, name).toBeGreaterThan(0.95);
    }
  });

  it("reports fairness only where fairness is a question", () => {
    // Single-key traces have no fairness to measure, and writing zero would
    // read as "maximally unfair" — a lie. The metric is absent instead.
    for (const [name, file] of bench) {
      expect(file.traces["many-keys"]!.metrics["jainFairness"], name).toBeGreaterThan(0);
      expect(file.traces["steady"]!.metrics["jainFairness"], name).toBeUndefined();
      expect(file.traces["overload"]!.metrics["jainFairness"], name).toBeUndefined();
    }
  });
});

describe("properties the READMEs claim", () => {
  const spec = RATE_LIMITER_TRACES["steady"]!;

  it("has the token bucket and GCRA agreeing exactly on a real trace", () => {
    // The three-names-one-algorithm claim, checked on a canonical workload
    // rather than only on randomised sequences.
    const trace = generateRateLimiterTrace("steady");
    const options = { keyUniverse: spec.keyUniverse };

    const byTokens = runRateLimiterTrace(
      new TokenBucket({ ratePerSec: 100, burst: 100 }),
      trace,
      options,
    );
    const byGcra = runRateLimiterTrace(new Gcra({ ratePerSec: 100, burst: 100 }), trace, options);

    expect(byGcra.accepted).toBe(byTokens.accepted);
    expect(byGcra.maxBurst100ms).toBe(byTokens.maxBurst100ms);
    expect(rateLimiterMetrics(byGcra)).toEqual(rateLimiterMetrics(byTokens));
  });

  it("has the sliding log holding its limit over every window on `bursty`", () => {
    // Its exactness claim, checked on the trace most likely to break it: a
    // burst of 100 arrivals inside 200 ms, thirty times over. No window of any
    // offset may contain more than the limit.
    const trace = generateRateLimiterTrace("bursty");
    const limit = 100;
    const windowMs = 1_000;
    const policy = new SlidingLog({ limit, windowMs });

    const admitted: number[] = [];
    for (let index = 0; index < trace.times.length; index += 1) {
      const now = trace.times[index]!;
      if (policy.allow(trace.keys[index]!, 1, now)) admitted.push(now);
    }

    expect(admitted.length).toBeGreaterThan(2_000);
    // Every window that could be maximal ends at an admitted timestamp.
    for (const end of admitted) {
      const inWindow = admitted.filter((t) => t > end - windowMs && t <= end).length;
      expect(inWindow).toBeLessThanOrEqual(limit);
    }
  });
});
