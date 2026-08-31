/**
 * The compare page's data, and the one assumption its chart rests on.
 *
 * The page itself is Astro and the chart is canvas, neither of which is worth
 * asserting on directly. What is worth asserting on is what they are built
 * from: the preset each policy page links to, and the fact that all three
 * runnable domains expose the same shape of series — which is the only reason
 * one overlay renderer can serve all of them.
 */

import { describe, expect, it } from "vitest";
import { allPolicies, headlineMetric, loadCatalog, neighboursOf } from "./catalog";
import { RUNNABLE_DOMAINS, createSimulation } from "./simulation";
import { TRACES_BY_DOMAIN } from "./traces";
import Lru from "../../../../policies/cache/lru/index";
import TokenBucket from "../../../../policies/rate-limiter/token-bucket/index";
import H2o from "../../../../policies/kv-cache/h2o/index";

describe("the overlay's one assumption", () => {
  /**
   * Every runnable domain's view exposes `history`, meaning "the metric this
   * domain is judged by, so far". `CompareRunner` reads exactly that field and
   * nothing else, so a domain that stopped providing it would draw an empty
   * chart rather than fail — which is the kind of silence this test exists to
   * break.
   */
  it.each([
    ["cache", (p: Record<string, unknown>) => new Lru(p as { capacity: number }), { capacity: 64 }, "zipf-1.0-100k"],
    ["rate-limiter", (p: Record<string, unknown>) => new TokenBucket(p as never), { ratePerSec: 100, burst: 100, maxKeys: 128 }, "bursty"],
    ["kv-cache", (p: Record<string, unknown>) => new H2o(p as { budget: number }), { budget: 128 }, "decode-4096"],
  ])("gives %s a history the overlay can draw", (domain, build, params, trace) => {
    const simulation = createSimulation(domain, build, params, trace, 2_000);
    simulation.seek(simulation.totalSteps);
    const view = simulation.frame().view;

    expect(view).toHaveProperty("history");
    const history = (view as { history: number[] }).history;
    expect(Array.isArray(history)).toBe(true);
    expect(history.length).toBeGreaterThan(1);
    for (const value of history) {
      expect(Number.isFinite(value)).toBe(true);
    }
  });

  it("covers every runnable domain", () => {
    // If a fourth runnable domain appears, the case list above must grow with
    // it rather than quietly testing three of four.
    expect([...RUNNABLE_DOMAINS].sort()).toEqual(["cache", "kv-cache", "rate-limiter"]);
  });
});

describe("the comparison a policy page links to", () => {
  it("offers real neighbours for every runnable policy", () => {
    const runnable = allPolicies().filter(
      (policy) =>
        (RUNNABLE_DOMAINS as readonly string[]).includes(policy.domain) &&
        policy.meta.status !== "offline-bound",
    );
    expect(runnable.length).toBeGreaterThan(10);

    const slugs = new Set(allPolicies().map((policy) => `${policy.domain}/${policy.slug}`));

    for (const policy of runnable) {
      const neighbours = neighboursOf(policy);
      expect(neighbours, policy.slug).toHaveLength(2);

      // Real policies, in the same domain, and never the policy itself.
      for (const slug of neighbours) {
        expect(slugs.has(`${policy.domain}/${slug}`), `${policy.slug} → ${slug}`).toBe(true);
        expect(slug).not.toBe(policy.slug);
      }
      expect(new Set(neighbours).size).toBe(neighbours.length);
    }
  });

  it("never proposes an offline bound as a candidate", () => {
    // OPT needs the whole future; comparing against it would invite a reader to
    // treat the ceiling as something they could deploy.
    const offline = new Set(
      allPolicies()
        .filter((policy) => policy.meta.status === "offline-bound")
        .map((policy) => policy.slug),
    );
    expect(offline.size).toBeGreaterThan(0);

    for (const policy of allPolicies()) {
      for (const slug of neighboursOf(policy)) {
        expect(offline.has(slug), `${policy.slug} → ${slug}`).toBe(false);
      }
    }
  });

  it("picks the policies adjacent to it in the ranking", () => {
    /*
     * The point of the preset, stated exactly.
     *
     * "Its neighbours score close by" would be no test at all — any two scores
     * in a set differ by at most that set's spread, so the assertion would hold
     * for an alphabetical list or a random one. What has to be true is
     * *adjacency*: the chosen slugs occupy a contiguous run of the ranking that
     * includes the policy itself, so nothing better or worse was skipped over
     * to reach them.
     */
    for (const domain of loadCatalog()) {
      if (!(RUNNABLE_DOMAINS as readonly string[]).includes(domain.id)) continue;

      const ranked = domain.policies
        .filter((policy) => policy.meta.status !== "offline-bound")
        .map((policy) => ({ slug: policy.slug, score: headlineMetric(policy) }))
        .filter((entry): entry is { slug: string; score: number } => entry.score !== null)
        .sort((a, b) => b.score - a.score || a.slug.localeCompare(b.slug))
        .map((entry) => entry.slug);
      if (ranked.length < 3) continue;

      for (const policy of domain.policies) {
        const neighbours = neighboursOf(policy);
        if (neighbours.length === 0) continue;

        const positions = [policy.slug, ...neighbours]
          .map((slug) => ranked.indexOf(slug))
          .sort((a, b) => a - b);

        expect(positions[0], `${policy.slug} has an unranked pick`).toBeGreaterThanOrEqual(0);
        // Contiguous: three ranks with no gap.
        expect(
          positions.at(-1)! - positions[0]!,
          `${policy.slug} → ${neighbours.join(", ")} skips a rank`,
        ).toBe(positions.length - 1);
      }
    }
  });
});

describe("the traces a compare page offers", () => {
  it("lists at least one for every runnable domain", () => {
    for (const domain of RUNNABLE_DOMAINS) {
      expect(TRACES_BY_DOMAIN[domain]?.length ?? 0, domain).toBeGreaterThan(0);
    }
  });
});
