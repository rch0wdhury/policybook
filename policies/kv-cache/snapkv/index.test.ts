import { describe, expect, it } from "vitest";
import { runKvCacheTrace } from "../../../packages/core/src/domains/kv-cache/harness";
import { kvCacheMetrics } from "../../../packages/core/src/domains/kv-cache/metrics";
import { KV_CACHE_TRACES } from "../../../packages/core/src/domains/kv-cache/traces";
import SlidingWindow from "../sliding-window/index";
import Tova from "../tova/index";
import SnapKv from "./index";

const SPEC = KV_CACHE_TRACES["decode-4096"]!;

const BUDGETS = [256, 512, 1_024];

describe("SnapKv", () => {
  it("rejects an even pool kernel, which would have no centre", () => {
    expect(() => new SnapKv({ poolKernel: 4 })).toThrow(/odd/);
    expect(() => new SnapKv({ poolKernel: 0 })).toThrow(/odd/);
    expect(() => new SnapKv({ obsWindow: 0 })).toThrow(RangeError);
    expect(() => new SnapKv({ budget: 8, recentWindow: 8 })).toThrow(/must be smaller than/);
  });

  it("ages a weight out of the window after obsWindow observations", () => {
    const policy = new SnapKv({ budget: 8, recentWindow: 1, obsWindow: 2, poolKernel: 1 });
    policy.onDecodeStep(1, Float32Array.from([1.0]));
    expect(policy.windowScoreOf(0)).toBe(1.0);
    policy.onDecodeStep(2, Float32Array.from([0.25, 0.75]));
    expect(policy.windowScoreOf(0)).toBe(1.25);
    // The third observation overwrites the first slot.
    policy.onDecodeStep(3, Float32Array.from([0.125, 0.375, 0.5]));
    expect(policy.windowScoreOf(0)).toBe(0.375);
  });

  it("treats a null attention vector as inert, not as a step of zeroes", () => {
    // Advancing the ring without writing would leave the 1.0 sitting in its
    // slot for another full cycle, so a window claiming to cover the recent
    // past would be summing a weight of indeterminate age.
    const policy = new SnapKv({ budget: 8, recentWindow: 1, obsWindow: 2, poolKernel: 1 });
    policy.onDecodeStep(1, Float32Array.from([1.0]));
    policy.onDecodeStep(2, null);
    policy.onDecodeStep(3, null);
    expect(policy.windowScoreOf(0)).toBe(1.0);
  });

  it("lets a high scorer defend its neighbours", () => {
    // The max-pool, in isolation. Position 3 scores almost nothing on its own
    // and sits beside position 2, which scores well; with pooling it inherits
    // that and survives, and position 0 goes instead.
    const build = (poolKernel: number): SnapKv =>
      new SnapKv({ budget: 4, recentWindow: 1, obsWindow: 8, poolKernel });
    const steps: number[][] = [
      [1.0],
      [0.5, 0.5],
      [0.0625, 0.0625, 0.875],
      [0.0625, 0.0625, 0.8125, 0.0625],
    ];

    const unpooled = build(1);
    const pooled = build(3);
    steps.forEach((weights, index) => {
      const attn = Float32Array.from(weights);
      unpooled.onDecodeStep(index + 1, attn);
      pooled.onDecodeStep(index + 1, attn);
    });

    // Identical unpooled scores, so the pooling is the only thing that differs.
    expect(unpooled.windowScoreOf(3)).toBe(0.0625);
    expect(pooled.windowScoreOf(3)).toBe(0.0625);

    expect(unpooled.evict(4)).toEqual([3]);
    expect(pooled.evict(4)).toEqual([0]);
  });

  describe("on the canonical trace", () => {
    it("beats a plain sliding window on both metrics, at every budget", () => {
      for (const budget of BUDGETS) {
        const window = kvCacheMetrics(
          runKvCacheTrace(new SlidingWindow({ budget }), SPEC, { budget }),
        );
        const snapkv = kvCacheMetrics(
          runKvCacheTrace(new SnapKv({ budget }), SPEC, { budget }),
        );

        expect(snapkv.retainedAttentionMass).toBeGreaterThan(window.retainedAttentionMass);
        expect(snapkv.heavyHitterRecall).toBeGreaterThan(window.heavyHitterRecall);
      }
    });

    it("reduces exactly to TOVA when the pooling is switched off", () => {
      // A strong claim, and it holds to the last decimal at every budget. It is
      // a fact about this workload rather than about the algorithms: ranking
      // positions by their attention over the last sixteen steps gives the same
      // order as ranking them by the latest step alone, because the trace is
      // stationary within a heavy-hitter epoch. Two independently written
      // policies agreeing exactly is also a decent check on both.
      for (const budget of BUDGETS) {
        const tova = kvCacheMetrics(runKvCacheTrace(new Tova({ budget }), SPEC, { budget }));
        const unpooled = kvCacheMetrics(
          runKvCacheTrace(new SnapKv({ budget, poolKernel: 1 }), SPEC, { budget }),
        );
        expect(unpooled).toEqual(tova);
      }
    });

    it("is unaffected by the observation window on this trace", () => {
      // The uncomfortable half. One of this policy's two mechanisms does
      // nothing measurable here, for the same reason as above: the ranking does
      // not change with how far back you look. That is a limitation of the
      // workload, not of the policy, and it is worth stating outright before
      // anyone reads the benchmark table as evidence about window sizes.
      for (const budget of BUDGETS) {
        const narrow = kvCacheMetrics(
          runKvCacheTrace(new SnapKv({ budget, obsWindow: 1 }), SPEC, { budget }),
        );
        const wide = kvCacheMetrics(
          runKvCacheTrace(new SnapKv({ budget, obsWindow: 64 }), SPEC, { budget }),
        );
        expect(narrow).toEqual(wide);
      }
    });

    it("gains and loses a little from the pooling, depending on the budget", () => {
      // So the pooling is the whole of the measurable difference from TOVA, and
      // it is not uniformly good: it helps at 256 and 1,024 and hurts at 512.
      // A few thousandths either way is the honest size of the effect.
      const at = (budget: number, poolKernel: number): number =>
        kvCacheMetrics(runKvCacheTrace(new SnapKv({ budget, poolKernel }), SPEC, { budget }))
          .retainedAttentionMass;

      expect(at(256, 7)).toBeGreaterThan(at(256, 1));
      expect(at(1_024, 7)).toBeGreaterThan(at(1_024, 1));
      expect(at(512, 7)).toBeLessThan(at(512, 1));

      for (const budget of BUDGETS) {
        expect(Math.abs(at(budget, 7) - at(budget, 1))).toBeLessThan(0.01);
      }
    });
  });
});
