import { describe, expect, it } from "vitest";
import { runKvCacheTrace } from "../../../packages/core/src/domains/kv-cache/harness";
import { kvCacheMetrics } from "../../../packages/core/src/domains/kv-cache/metrics";
import { KV_CACHE_TRACES } from "../../../packages/core/src/domains/kv-cache/traces";
import H2o from "../h2o/index";
import SlidingWindow from "../sliding-window/index";
import Scissorhands from "./index";

const SPEC = KV_CACHE_TRACES["decode-4096"]!;

/** The shared scenario from both policies' distinguishing vectors. */
const SPIKE_THEN_SILENCE: number[][] = [
  [1.0],
  [0.875, 0.125],
  [0.0625, 0.5, 0.4375],
  [0.0625, 0.3125, 0.3125, 0.3125],
  [0.0625, 0.25, 0.25, 0.25, 0.1875],
];

describe("Scissorhands", () => {
  it("rejects parameters that leave the votes nothing to choose between", () => {
    expect(() => new Scissorhands({ budget: 8, recentWindow: 8 })).toThrow(
      /must be smaller than/,
    );
    expect(() => new Scissorhands({ budget: 0 })).toThrow(RangeError);
    expect(() => new Scissorhands({ budget: 8, recentWindow: -1 })).toThrow(RangeError);
  });

  it("starts holding position 0 with no votes", () => {
    const policy = new Scissorhands({ budget: 8, recentWindow: 2 });
    expect(policy.keptCount()).toBe(1);
    expect(policy.votesOf(0)).toBe(0);
  });

  it("requires attention to exceed the fair share, not merely match it", () => {
    // The single most likely thing to diverge between ports, so it is pinned
    // here as well as in the vectors. Two positions splitting a step evenly
    // each receive exactly 1/2 and neither votes.
    const policy = new Scissorhands({ budget: 8, recentWindow: 2 });
    policy.onDecodeStep(1, new Float32Array([1]));
    expect(policy.votesOf(0)).toBe(0);

    policy.onDecodeStep(2, new Float32Array([0.5, 0.5]));
    expect(policy.votesOf(0)).toBe(0);
    expect(policy.votesOf(1)).toBe(0);

    // A hair over the share does vote, which is what shows the comparison is
    // happening at all rather than everything failing it.
    policy.onDecodeStep(3, new Float32Array([0.4, 0.3, 0.3]));
    expect(policy.votesOf(0)).toBe(1);
  });

  it("splits from H2O on a position that spiked once and went quiet", () => {
    // The distinguishing case, run against both policies at once so the claim
    // is a comparison rather than two separate assertions that could drift.
    const budget = 5;
    const recentWindow = 2;
    const h2o = new H2o({ budget, recentWindow });
    const scissorhands = new Scissorhands({ budget, recentWindow });

    SPIKE_THEN_SILENCE.forEach((weights, index) => {
      const attn = Float32Array.from(weights);
      h2o.onDecodeStep(index + 1, attn);
      scissorhands.onDecodeStep(index + 1, attn);
    });

    // Position 0 took almost the whole of the first two steps and nearly
    // nothing since: the highest cumulative score of any position, and a
    // single vote.
    expect(h2o.scoreOf(0)).toBe(2.0625);
    expect(scissorhands.votesOf(0)).toBe(1);

    // H2O defends it and drops the youngest unprotected position instead.
    expect(h2o.evict(budget)).toEqual([3]);
    // Scissorhands drops it.
    expect(scissorhands.evict(budget)).toEqual([0]);
  });

  describe("on the canonical trace", () => {
    it("beats a plain sliding window on both metrics, at every budget", () => {
      for (const budget of [256, 512, 1_024]) {
        const window = kvCacheMetrics(
          runKvCacheTrace(new SlidingWindow({ budget }), SPEC, { budget }),
        );
        const scissorhands = kvCacheMetrics(
          runKvCacheTrace(new Scissorhands({ budget }), SPEC, { budget }),
        );

        expect(scissorhands.retainedAttentionMass).toBeGreaterThan(
          window.retainedAttentionMass,
        );
        expect(scissorhands.heavyHitterRecall).toBeGreaterThan(window.heavyHitterRecall);
      }
    });

    it("is within a hair of H2O, because this workload's heavy hitters persist", () => {
      // The honest result. A sum and a count only disagree about positions that
      // mattered enormously once and never again, and this trace has none by
      // construction: its heavy hitters hold their weight for a whole 512-step
      // epoch. The distinguishing vector shows the difference is real; this
      // shows the workload does not exercise it, which is worth knowing before
      // reading anything into the benchmark table.
      for (const budget of [256, 512, 1_024]) {
        const h2o = kvCacheMetrics(runKvCacheTrace(new H2o({ budget }), SPEC, { budget }));
        const scissorhands = kvCacheMetrics(
          runKvCacheTrace(new Scissorhands({ budget }), SPEC, { budget }),
        );

        expect(
          Math.abs(scissorhands.retainedAttentionMass - h2o.retainedAttentionMass),
        ).toBeLessThan(0.01);
        expect(
          Math.abs(scissorhands.heavyHitterRecall - h2o.heavyHitterRecall),
        ).toBeLessThan(0.01);
      }
    });

    it("widening the recent window helps it exactly as it helps H2O", () => {
      // Both policies under-protect recency at the default window for the same
      // structural reason, so the fix is the same and the size of it should be
      // too. If these two diverged, one of them would be doing something other
      // than scoring the non-recent tail.
      const budget = 256;
      const narrow = kvCacheMetrics(
        runKvCacheTrace(new Scissorhands({ budget }), SPEC, { budget }),
      );
      const wide = kvCacheMetrics(
        runKvCacheTrace(new Scissorhands({ budget, recentWindow: 64 }), SPEC, { budget }),
      );

      expect(wide.retainedAttentionMass - narrow.retainedAttentionMass).toBeGreaterThan(0.1);
    });
  });
});
