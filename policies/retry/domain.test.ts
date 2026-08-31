/**
 * Properties the retry domain's published numbers must satisfy.
 *
 * These read the committed `bench.json` files rather than recomputing, so they
 * check what the READMEs actually claim — including the uncomfortable orderings
 * the pages state plainly, which is exactly the kind of thing that rots quietly.
 */

import { readFileSync } from "node:fs";
import { join } from "node:path";
import { describe, expect, it } from "vitest";
import { discoverPolicies, findRepoRoot } from "../../packages/vectors/src/discover";

interface BenchFile {
  policy: string;
  coreVersion: string;
  traces: Record<string, { metrics: Record<string, number>; perf: { opsPerSec: number } }>;
}

const repoRoot = findRepoRoot();
const policies = discoverPolicies(repoRoot).filter((policy) => policy.domain === "retry");

const bench = new Map<string, BenchFile>();
for (const policy of policies) {
  bench.set(
    policy.name,
    JSON.parse(readFileSync(join(policy.dir, "bench.json"), "utf8")) as BenchFile,
  );
}

const TRACE = "outage-30s";

/** A metric from the committed benchmark, by policy directory name. */
function metric(policy: string, name: string): number {
  const value = bench.get(policy)?.traces[TRACE]?.metrics[name];
  if (value === undefined) throw new Error(`no ${name} for retry/${policy}`);
  return value;
}

describe("the published retry benchmark", () => {
  it("covers every policy", () => {
    expect(policies.length).toBe(6);
    for (const [name, file] of bench) {
      expect(Object.keys(file.traces), name).toEqual([TRACE]);
    }
  });

  it("is all stable, now that it has numbers", () => {
    for (const policy of policies) {
      expect(policy.meta.status, policy.id).toBe("stable");
      expect(policy.meta.ports.sort(), policy.id).toEqual(["c", "python", "ts"]);
    }
  });

  it("keeps full jitter below plain exponential on success", () => {
    // The uncomfortable half of the result, which both READMEs state. A uniform
    // draw from [0, ceiling] averages half of it, so full jitter exhausts the
    // same attempt budget in half the elapsed time and reaches fewer
    // recoveries. If this ever silently flipped, the pages would be wrong.
    expect(metric("exponential-full-jitter", "successRate")).toBeLessThan(
      metric("exponential", "successRate"),
    );
  });

  it("orders the jitter family exactly as the arithmetic predicts", () => {
    // Expected delays of one, three quarters and one half of the ceiling, so
    // success rates in that order.
    expect(metric("exponential", "successRate")).toBeGreaterThan(
      metric("equal-jitter", "successRate"),
    );
    expect(metric("equal-jitter", "successRate")).toBeGreaterThan(
      metric("exponential-full-jitter", "successRate"),
    );
  });

  it("shows the un-jittered policies synchronising alike despite different curves", () => {
    // Backing off exponentially converts a continuous herd into a periodic one;
    // it does not disperse it. Constant and exponential differ enormously in
    // *when* they retry and hardly at all in how synchronised they are.
    const constant = metric("constant", "peakRetryShare");
    const exponential = metric("exponential", "peakRetryShare");
    expect(Math.abs(constant - exponential)).toBeLessThan(0.05);
    expect(Math.min(constant, exponential)).toBeGreaterThan(0.1);
  });

  it("gives every jittered policy a far smaller herd", () => {
    const unjittered = metric("exponential", "peakRetryShare");
    for (const name of ["exponential-full-jitter", "equal-jitter", "decorrelated-jitter"]) {
      expect(metric(name, "peakRetryShare"), name).toBeLessThan(unjittered / 4);
    }
  });

  it("gives decorrelated jitter the smallest herd and the longest wait", () => {
    // Whole schedules diverge rather than individual attempts, so it disperses
    // best; and it climbs by about 1.5x a step against exponential's 2x, so it
    // covers less elapsed time per attempt. The two facts are the same fact.
    const decorrelated = metric("decorrelated-jitter", "peakRetryShare");
    for (const name of ["exponential-full-jitter", "equal-jitter", "constant", "exponential"]) {
      expect(decorrelated, name).toBeLessThan(metric(name, "peakRetryShare"));
    }
    expect(metric("decorrelated-jitter", "meanTimeToSuccessMs")).toBeGreaterThan(
      metric("exponential-full-jitter", "meanTimeToSuccessMs"),
    );
  });

  it("shows reading the server's answer buying success and costing dispersal", () => {
    // Both halves, because a page that reported only the first would be
    // recommending a herd.
    expect(metric("retry-after-aware", "successRate")).toBeGreaterThan(0.95);
    expect(metric("retry-after-aware", "peakRetryShare")).toBeGreaterThan(
      metric("constant", "peakRetryShare"),
    );
  });

  it("leaves every policy short of the outage on the reference budget", () => {
    // Eight attempts at a 100 ms base is what a default HTTP client does, and
    // it does not survive a 30-second outage. The READMEs say so; this makes
    // sure they stay right.
    for (const [name, file] of bench) {
      if (name === "retry-after-aware") continue; // told the answer, not guessing
      expect(file.traces[TRACE]!.metrics["successRate"], name).toBeLessThan(0.5);
    }
  });
});
