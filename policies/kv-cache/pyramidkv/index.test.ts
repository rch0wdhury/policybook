import { readFileSync } from "node:fs";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { runKvCacheTrace } from "../../../packages/core/src/domains/kv-cache/harness";
import { kvCacheMetrics } from "../../../packages/core/src/domains/kv-cache/metrics";
import { KV_CACHE_TRACES } from "../../../packages/core/src/domains/kv-cache/traces";
import SnapKv from "../snapkv/index";
import PyramidKv, { pyramidBudget } from "./index";

const SPEC = KV_CACHE_TRACES["decode-4096"]!;

describe("pyramidBudget", () => {
  it("returns the budget unchanged for a single layer", () => {
    // The default, and the configuration the registry's benchmark runs.
    expect(pyramidBudget(512, 0, 1, 4)).toBe(512);
    expect(pyramidBudget(512, 0, 1, 99)).toBe(512);
  });

  it("preserves the total, so it redistributes rather than inflates", () => {
    // The property that makes this an allocation rather than a bigger cache.
    for (const numLayers of [2, 3, 4, 8, 32]) {
      for (const ratio of [1, 2, 4, 10]) {
        let total = 0;
        for (let layer = 0; layer < numLayers; layer += 1) {
          total += pyramidBudget(512, layer, numLayers, ratio);
        }
        // Integer division floors each term, so the sum can fall a little short
        // of numLayers x budget but must never exceed it.
        const exact = 512 * numLayers;
        expect(total).toBeLessThanOrEqual(exact);
        expect(total).toBeGreaterThan(exact - numLayers);
      }
    }
  });

  it("decreases with depth, and by the ratio end to end", () => {
    const numLayers = 8;
    const ratio = 4;
    const shares = Array.from({ length: numLayers }, (_, layer) =>
      pyramidBudget(512, layer, numLayers, ratio),
    );

    for (let i = 1; i < shares.length; i += 1) {
      expect(shares[i]!).toBeLessThan(shares[i - 1]!);
    }
    // First over last is the ratio, to within the flooring.
    expect(shares[0]! / shares[numLayers - 1]!).toBeCloseTo(ratio, 1);
  });

  it("gives the middle layer of an odd stack exactly the average", () => {
    // An arithmetic sequence is symmetric about its mean, whatever the ratio.
    for (const ratio of [1, 2, 4, 7]) {
      expect(pyramidBudget(600, 1, 3, ratio)).toBe(600);
      expect(pyramidBudget(600, 2, 5, ratio)).toBe(600);
    }
  });

  it("is uniform at a ratio of one", () => {
    for (let layer = 0; layer < 5; layer += 1) {
      expect(pyramidBudget(300, layer, 5, 1)).toBe(300);
    }
  });
});

describe("PyramidKv", () => {
  it("rejects a layer outside the stack", () => {
    expect(() => new PyramidKv({ numLayers: 4, layer: 4 })).toThrow(/layer must be/);
    expect(() => new PyramidKv({ numLayers: 4, layer: -1 })).toThrow(/layer must be/);
    expect(() => new PyramidKv({ pyramidRatio: 0 })).toThrow(/at least 1/);
    expect(() => new PyramidKv({ poolKernel: 4 })).toThrow(/odd/);
  });

  it("raises a share that would fall below the recent window", () => {
    // A cache smaller than its own protected region is a state the selection
    // rule cannot express — the score would have nothing to choose between.
    const policy = new PyramidKv({
      budget: 10,
      layer: 3,
      numLayers: 4,
      pyramidRatio: 4,
      recentWindow: 4,
    });
    expect(pyramidBudget(10, 3, 4, 4)).toBe(4);
    expect(policy.effectiveBudget()).toBe(5);
  });

  it("is exactly SnapKV on a single layer", () => {
    // Not "close to" — the same program. With numLayers 1 the allocation
    // returns the budget unchanged and nothing else differs, which is why the
    // benchmark rows for the two policies are identical and why the READMEs
    // say so rather than letting the table imply a comparison happened.
    for (const budget of [256, 512, 1_024]) {
      const pyramid = kvCacheMetrics(
        runKvCacheTrace(new PyramidKv({ budget }), SPEC, { budget }),
      );
      const snap = kvCacheMetrics(runKvCacheTrace(new SnapKv({ budget }), SPEC, { budget }));
      expect(pyramid).toEqual(snap);
    }
  });

  it("evicts a deep layer down to its own share, not to the offered budget", () => {
    // The allocation doing its job end to end, rather than only in the unit
    // tests above.
    //
    // Measured immediately after an eviction, because a policy cannot evict
    // itself: the caller decides *when* to ask, and this harness asks at its
    // own budget of 512. So a deep layer climbs to 513, is asked once, and
    // drops to its 204 — sawtoothing rather than sitting at its share. A real
    // deployment asks per layer at that layer's budget and the residency stays
    // put; the README says so, because the peak memory saving the paper is
    // after is not visible in this harness even though the retained-mass effect
    // below is.
    const budget = 512;
    const shallow = new PyramidKv({ budget, layer: 0, numLayers: 4, pyramidRatio: 4 });
    const deep = new PyramidKv({ budget, layer: 3, numLayers: 4, pyramidRatio: 4 });

    runKvCacheTrace(shallow, SPEC, { budget, maxSteps: 2_000 });
    runKvCacheTrace(deep, SPEC, { budget, maxSteps: 2_000 });

    expect(deep.effectiveBudget()).toBe(204);
    expect(shallow.effectiveBudget()).toBe(819);

    shallow.evict(budget);
    deep.evict(budget);

    // Layer 0's share exceeds the offer, so the offer caps it; layer 3's share
    // is tighter than the offer, so the share caps it.
    expect(shallow.keptCount()).toBe(budget);
    expect(deep.keptCount()).toBe(deep.effectiveBudget());
    expect(deep.keptCount()).toBeLessThan(budget);
  });

  it("retains less attention at a deep layer, which is the cost being paid", () => {
    // The pyramid is a trade, not a free win: the deep layer gives up mass in
    // exchange for the memory a shallow layer spends. A test that only checked
    // the allocation arithmetic would not show that anything was surrendered.
    const budget = 512;
    const shallow = kvCacheMetrics(
      runKvCacheTrace(
        new PyramidKv({ budget, layer: 0, numLayers: 4, pyramidRatio: 4 }),
        SPEC,
        { budget },
      ),
    );
    const deep = kvCacheMetrics(
      runKvCacheTrace(
        new PyramidKv({ budget, layer: 3, numLayers: 4, pyramidRatio: 4 }),
        SPEC,
        { budget },
      ),
    );

    expect(deep.retainedAttentionMass).toBeLessThan(shallow.retainedAttentionMass);
  });

  it("benches as exactly SnapKV, and the committed files agree byte for byte", () => {
    // The README says the benchmark row is "SnapKV's numbers, reproduced",
    // because at the default numLayers of 1 the pyramid degenerates and the
    // two policies make identical decisions. A claim like that drifts unless
    // something holds it: if either default ever changes, this fails and the
    // README's story has to change with it.
    const here = fileURLToPath(new URL(".", import.meta.url));
    const read = (name: string) =>
      JSON.parse(readFileSync(join(here, "..", name, "bench.json"), "utf8")) as {
        traces: Record<string, { metrics: Record<string, number> }>;
      };
    const mine = read("pyramidkv");
    const snap = read("snapkv");

    expect(Object.keys(mine.traces)).toEqual(Object.keys(snap.traces));
    for (const [trace, row] of Object.entries(mine.traces)) {
      // Metrics only: throughput is measured, not derived, and may wobble.
      expect(row.metrics, trace).toEqual(snap.traces[trace]!.metrics);
    }
  });
});
