/**
 * Properties the kv-cache domain's published numbers must satisfy.
 *
 * These read the committed `bench.json` files rather than recomputing, so they
 * check what the READMEs actually claim — including the awkward parts, which are
 * most of what this domain's pages are for. Three policies here are close to or
 * identical to another, and one loses to a much simpler policy on the headline
 * metric; every one of those is stated on a page, and every one is pinned below.
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
const policies = discoverPolicies(repoRoot).filter((policy) => policy.domain === "kv-cache");

const bench = new Map<string, BenchFile>();
for (const policy of policies) {
  bench.set(
    policy.name,
    JSON.parse(readFileSync(join(policy.dir, "bench.json"), "utf8")) as BenchFile,
  );
}

const BUDGETS = [256, 512, 1_024] as const;
const MASS = "retainedAttentionMass";
const RECALL = "heavyHitterRecall";

/** A metric from the committed benchmark, by policy directory name and budget. */
function metric(policy: string, name: string, budget: number): number {
  const value = bench.get(policy)?.traces[`decode-4096@${budget}`]?.metrics[name];
  if (value === undefined) throw new Error(`no ${name} for kv-cache/${policy} at ${budget}`);
  return value;
}

describe("the published kv-cache benchmark", () => {
  it("covers every policy at every canonical budget", () => {
    expect(policies.length).toBe(7);
    for (const [name, file] of bench) {
      expect(Object.keys(file.traces), name).toEqual(
        BUDGETS.map((budget) => `decode-4096@${budget}`),
      );
    }
  });

  it("is all stable, now that it has numbers", () => {
    for (const policy of policies) {
      expect(policy.meta.status, policy.id).toBe("stable");
      expect(policy.meta.ports.sort(), policy.id).toEqual(["c", "python", "ts"]);
    }
  });

  it("puts every policy above the sliding-window baseline on both metrics", () => {
    // The domain's minimum claim: if a policy cannot beat "keep the most recent
    // and forget the rest", it has no reason to be here.
    for (const budget of BUDGETS) {
      for (const [name] of bench) {
        if (name === "sliding-window") continue;
        expect(metric(name, MASS, budget), `${name} mass @${budget}`).toBeGreaterThan(
          metric("sliding-window", MASS, budget),
        );
        expect(metric(name, RECALL, budget), `${name} recall @${budget}`).toBeGreaterThan(
          metric("sliding-window", RECALL, budget),
        );
      }
    }
  });

  it("keeps StreamingLLM above the plain window, which is its whole claim", () => {
    for (const budget of BUDGETS) {
      expect(metric("streaming-llm", MASS, budget)).toBeGreaterThan(
        metric("sliding-window", MASS, budget),
      );
    }
  });

  it("has the two metrics disagree about who leads, but only at the wider budgets", () => {
    // The reason the domain page refuses to publish a single ranking — and the
    // reason it says *which* budgets, because the disagreement is not universal.
    // At 256 the same policy leads both; at 512 and 1,024 they split. A page
    // claiming the metrics always disagree would be as wrong as one claiming
    // they always agree.
    const leader = (name: string, budget: number): string =>
      [...bench.keys()].sort((a, b) => metric(b, name, budget) - metric(a, name, budget))[0]!;

    expect(leader(MASS, 256)).toBe(leader(RECALL, 256));
    expect(leader(MASS, 512)).not.toBe(leader(RECALL, 512));
    expect(leader(MASS, 1_024)).not.toBe(leader(RECALL, 1_024));
  });

  it("leaves H2O below StreamingLLM on mass at the default recent window", () => {
    // The uncomfortable result, stated on three pages: a policy that reads
    // attention scoring below one that ignores it, because its fixed
    // 32-position recent window under-protects a 64-wide recency band. If this
    // silently flipped, the pages explaining it would be wrong.
    for (const budget of BUDGETS) {
      expect(metric("h2o", MASS, budget)).toBeLessThan(metric("streaming-llm", MASS, budget));
    }
  });

  it("gives H2O the best heavy-hitter recall at the wider budgets, not at 256", () => {
    // H2O's recall advantage is real but budget-dependent, which the domain
    // page now says rather than claiming it holds everywhere. At 512 it leads
    // outright; at 1,024 Scissorhands draws level; at 256 SnapKV is ahead of it.
    for (const [name] of bench) {
      if (name === "h2o") continue;
      expect(metric("h2o", RECALL, 512), name).toBeGreaterThan(metric(name, RECALL, 512));
      expect(metric("h2o", RECALL, 1_024), name).toBeGreaterThanOrEqual(
        metric(name, RECALL, 1_024),
      );
    }

    expect(metric("snapkv", RECALL, 256)).toBeGreaterThan(metric("h2o", RECALL, 256));
  });

  it("keeps H2O and Scissorhands within a hundredth of each other", () => {
    // A statement about the workload, not the policies: this trace's heavy
    // hitters persist for a whole epoch, so a cumulative sum and a vote count
    // pick the same positions. Both READMEs say so; if a future trace separates
    // them, this test is where that gets noticed.
    for (const budget of BUDGETS) {
      expect(Math.abs(metric("h2o", MASS, budget) - metric("scissorhands", MASS, budget))).toBeLessThan(0.01);
      expect(
        Math.abs(metric("h2o", RECALL, budget) - metric("scissorhands", RECALL, budget)),
      ).toBeLessThan(0.01);
    }
  });

  it("gives PyramidKV exactly SnapKV's numbers, because the trace has one layer", () => {
    // Not a tie — the same program. The domain README says the row is identical
    // by construction rather than by comparison, and this is what keeps that
    // sentence true.
    for (const budget of BUDGETS) {
      expect(metric("pyramidkv", MASS, budget)).toBe(metric("snapkv", MASS, budget));
      expect(metric("pyramidkv", RECALL, budget)).toBe(metric("snapkv", RECALL, budget));
    }
  });

  it("gives TOVA the best retained mass at the reference budget", () => {
    // The domain page recommends it on this basis, so the basis is pinned.
    for (const [name] of bench) {
      if (name === "tova") continue;
      expect(metric("tova", MASS, 512), name).toBeGreaterThan(metric(name, MASS, 512));
    }
  });

  it("improves every policy as the budget grows", () => {
    // A sanity check on the harness as much as the policies: a bigger cache
    // that retained less attention would mean something was badly wrong.
    for (const [name] of bench) {
      expect(metric(name, MASS, 512), name).toBeGreaterThan(metric(name, MASS, 256));
      expect(metric(name, MASS, 1_024), name).toBeGreaterThan(metric(name, MASS, 512));
    }
  });
});
