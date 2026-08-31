/**
 * The tutorial's example policy, and the claims the page makes about it.
 *
 * Step five tells the reader to predict what happens *before* pressing play:
 * the cache fills with the first N distinct keys and then never changes, and
 * the gap to LRU widens once the workload moves on. Those are falsifiable
 * statements printed on a page, so they are tested here rather than trusted.
 *
 * It also checks the policy is loadable by the runner under the id the page
 * uses. Without that the tutorial's centrepiece is a code listing beside a
 * broken control.
 */

import { describe, expect, it } from "vitest";
import Lru from "../../../../policies/cache/lru/index";
import { loadablePolicies } from "../lib/policy-modules";
import { createSimulation } from "../lib/simulation";
import EvictNewest from "./evict-newest";

const CAPACITY = 1_000;

function run(build: (params: Record<string, unknown>) => unknown, trace: string) {
  const simulation = createSimulation("cache", build, { capacity: CAPACITY }, trace);
  simulation.seek(simulation.totalSteps);
  const frame = simulation.frame();
  if (frame.view.kind !== "cache") throw new Error("expected a cache view");
  return { view: frame.view, hitRate: frame.metrics["hitRate"]! };
}

const EVICT_NEWEST = (p: Record<string, unknown>) => new EvictNewest(p as { capacity: number });
const LRU = (p: Record<string, unknown>) => new Lru(p as { capacity: number });

describe("the runner can load it", () => {
  it("exposes it under the id the tutorial page uses", () => {
    // The page passes policies={["tutorial/evict-newest", "lru"]}.
    expect(loadablePolicies()).toContain("tutorial/evict-newest");
  });

  it("does not smuggle it in among the real policies", () => {
    // It has no paper, no vectors and no business being `policybook add`ed.
    // Every registry id has a domain prefix from `policies/`; this one does not.
    const registry = loadablePolicies().filter((id) => !id.startsWith("tutorial/"));
    expect(registry).not.toContain("cache/evict-newest");
    expect(registry.length).toBeGreaterThan(20);
  });
});

describe("the claims step five makes", () => {
  it("freezes after the first capacity-many distinct keys", () => {
    // "The cache fills with the first thousand distinct keys and then never
    // changes again." Checked directly: the resident set after a short run and
    // after a long one are the same.
    const early = createSimulation("cache", EVICT_NEWEST, { capacity: CAPACITY }, "zipf-1.0-100k");
    early.seek(6_000);
    const earlyFrame = early.frame();
    if (earlyFrame.view.kind !== "cache") throw new Error("expected a cache view");

    const late = run(EVICT_NEWEST, "zipf-1.0-100k").view;

    expect(earlyFrame.view.resident.length).toBe(CAPACITY);
    expect([...late.resident].sort((a, b) => a - b)).toEqual(
      [...earlyFrame.view.resident].sort((a, b) => a - b),
    );
  });

  it("evicts exactly the key it was just given, once full", () => {
    // The mechanism behind the freeze, stated as the page states it: every
    // eviction undoes the insert that caused it.
    const policy = new EvictNewest({ capacity: 2 });
    policy.onAccess(1, false);
    policy.onAccess(2, false);
    policy.onAccess(3, false);
    expect(policy.evict()).toBe(3);
  });

  it("loses to LRU, and loses by more once popularity shifts", () => {
    // "The gap widens as the trace moves on and EvictNewest's frozen cache
    // keeps answering yesterday's question."
    const steady = run(EVICT_NEWEST, "zipf-1.0-100k").hitRate;
    const steadyLru = run(LRU, "zipf-1.0-100k").hitRate;
    const shifting = run(EVICT_NEWEST, "shifting-popularity").hitRate;
    const shiftingLru = run(LRU, "shifting-popularity").hitRate;

    expect(steady).toBeLessThan(steadyLru);
    expect(shifting).toBeLessThan(shiftingLru);
    expect(shiftingLru - shifting).toBeGreaterThan(steadyLru - steady);
  });

  it("still honours the interface it is a bad example of", () => {
    // A teaching example that broke the contract would teach the wrong thing.
    // Every key it returns must have been resident.
    const policy = new EvictNewest({ capacity: 3 });
    const resident = new Set<number>();
    for (const key of [1, 2, 3, 4, 5, 6]) {
      const hit = resident.has(key);
      policy.onAccess(key, hit);
      if (hit) continue;
      resident.add(key);
      while (resident.size > 3) {
        const victim = policy.evict();
        expect(resident.has(victim)).toBe(true);
        resident.delete(victim);
      }
    }
  });

  it("throws rather than returning nonsense when asked to evict from empty", () => {
    expect(() => new EvictNewest({ capacity: 4 }).evict()).toThrow(/nothing resident/);
  });
});
