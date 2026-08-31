/**
 * Properties the cache domain's published numbers must satisfy.
 *
 * These read the committed `bench.json` files rather than recomputing, so they
 * check what the READMEs actually claim. A stale or wrong benchmark is exactly
 * as damaging as a wrong implementation — arguably worse, since a reader
 * choosing a policy sees only the table.
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
const policies = discoverPolicies(repoRoot).filter((policy) => policy.domain === "cache");

const bench = new Map<string, BenchFile>();
for (const policy of policies) {
  bench.set(
    policy.name,
    JSON.parse(readFileSync(join(policy.dir, "bench.json"), "utf8")) as BenchFile,
  );
}

const TRACES = ["zipf-1.0-100k", "zipf-0.75-1m", "scan-heavy", "shifting-popularity"];

/** Hit rate for a policy on a trace, as published. */
function hitRate(policy: string, trace: string): number {
  const value = bench.get(policy)?.traces[trace]?.metrics["hitRate"];
  if (value === undefined) throw new Error(`no published hit rate for ${policy} on ${trace}`);
  return value;
}

describe("cache domain benchmarks", () => {
  it("publishes every policy on every canonical trace", () => {
    expect(bench.size).toBe(policies.length);
    for (const [name, file] of bench) {
      expect(Object.keys(file.traces).sort(), `${name} traces`).toEqual([...TRACES].sort());
    }
  });

  it("puts the offline bound above every online policy", () => {
    // The property that makes OPT worth publishing. A violation means a bug in
    // a policy, the harness, or the trace — not a clever policy.
    for (const trace of TRACES) {
      const bound = hitRate("opt", trace);
      for (const policy of policies) {
        if (policy.name === "opt") continue;
        expect(hitRate(policy.name, trace), `${policy.name} on ${trace}`).toBeLessThanOrEqual(bound);
      }
    }
  });

  it("shows scan-resistant policies beating FIFO and LRU on scan-heavy", () => {
    // The trace exists to separate these, and the README says it does.
    const baseline = Math.max(hitRate("fifo", "scan-heavy"), hitRate("lru", "scan-heavy"));
    for (const policy of ["sieve", "s3-fifo", "w-tinylfu", "arc", "2q"]) {
      expect(hitRate(policy, "scan-heavy"), policy).toBeGreaterThan(baseline);
    }
  });

  it("shows LFU collapsing when popularity shifts", () => {
    // LFU's documented failure, and the reason the trace exists. If this ever
    // stops being true the README's central claim about LFU is wrong.
    const lfu = hitRate("lfu", "shifting-popularity");
    expect(lfu).toBeLessThan(hitRate("lru", "shifting-popularity") - 0.2);
    // On the stationary trace it is competitive, which is the other half of
    // the story.
    expect(hitRate("lfu", "zipf-1.0-100k")).toBeGreaterThan(hitRate("lru", "zipf-1.0-100k"));
  });

  it("keeps FIFO as the floor on the stationary and scan traces", () => {
    for (const trace of ["zipf-1.0-100k", "zipf-0.75-1m", "scan-heavy"]) {
      const floor = hitRate("fifo", trace);
      for (const policy of ["lru", "clock", "sieve", "s3-fifo", "w-tinylfu", "arc", "2q"]) {
        expect(hitRate(policy, trace), `${policy} on ${trace}`).toBeGreaterThan(floor);
      }
    }
  });

  it("records SIEVE falling below FIFO when popularity rotates", () => {
    // Not a bug, and worth pinning so it cannot quietly change. SIEVE's visited
    // bits grant a second chance to keys from the *previous* popular set, so at
    // capacities large relative to the rotation it holds stale entries that
    // plain FIFO would have cleared on schedule. The effect reverses at small
    // capacities, where the whole cache turns over faster than the rotation.
    //
    // This is documented in SIEVE's README under "when not to use it". If this
    // assertion ever fails, that section needs revisiting rather than deleting.
    expect(hitRate("sieve", "shifting-popularity")).toBeLessThan(
      hitRate("fifo", "shifting-popularity"),
    );
    // Everything else still clears the floor on that trace.
    for (const policy of ["lru", "clock", "s3-fifo", "w-tinylfu", "arc", "2q"]) {
      expect(hitRate(policy, "shifting-popularity"), policy).toBeGreaterThan(
        hitRate("fifo", "shifting-popularity"),
      );
    }
  });

  it("leaves a real gap between the best online policy and the bound", () => {
    // If the gap were negligible the bound would not be worth publishing, and
    // the domain README's advice to read it would be misleading.
    for (const trace of TRACES) {
      const bound = hitRate("opt", trace);
      const best = Math.max(
        ...policies.filter((p) => p.name !== "opt").map((p) => hitRate(p.name, trace)),
      );
      expect(bound - best, `gap on ${trace}`).toBeGreaterThan(0.01);
    }
  });

  it("records a throughput figure for every run", () => {
    for (const [name, file] of bench) {
      for (const [trace, result] of Object.entries(file.traces)) {
        expect(result.perf.opsPerSec, `${name} on ${trace}`).toBeGreaterThan(0);
      }
    }
  });
});
