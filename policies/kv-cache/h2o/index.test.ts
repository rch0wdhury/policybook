import { describe, expect, it } from "vitest";
import { runKvCacheTrace } from "../../../packages/core/src/domains/kv-cache/harness";
import { kvCacheMetrics } from "../../../packages/core/src/domains/kv-cache/metrics";
import { KV_CACHE_TRACES } from "../../../packages/core/src/domains/kv-cache/traces";
import SlidingWindow from "../sliding-window/index";
import StreamingLlm from "../streaming-llm/index";
import H2o from "./index";

const SPEC = KV_CACHE_TRACES["decode-4096"]!;

/** The width of the trace's recency component; see the domain's TRACES.md. */
const TRACE_LOCAL_SPAN = 64;

describe("H2o", () => {
  it("rejects parameters that leave the score nothing to choose between", () => {
    expect(() => new H2o({ budget: 8, recentWindow: 8 })).toThrow(/must be smaller than/);
    expect(() => new H2o({ budget: 8, recentWindow: 12 })).toThrow(/must be smaller than/);
    expect(() => new H2o({ budget: 0 })).toThrow(RangeError);
    expect(() => new H2o({ budget: 8, recentWindow: -1 })).toThrow(RangeError);
  });

  it("starts holding position 0 with no score", () => {
    const policy = new H2o({ budget: 8, recentWindow: 2 });
    expect(policy.keptCount()).toBe(1);
    expect(policy.scoreOf(0)).toBe(0);
  });

  it("reports -1 for a position it does not hold", () => {
    expect(new H2o({ budget: 8, recentWindow: 2 }).scoreOf(99)).toBe(-1);
  });

  it("accumulates in float64, so a long run of small weights stays exact", () => {
    // Accumulating float32 weights in float32 would start losing low-order bits
    // once the running total is large relative to the addend, and the eviction
    // comparison depends on exactly those bits.
    const policy = new H2o({ budget: 4_096, recentWindow: 32 });
    const weight = Math.fround(0.1);
    for (let step = 1; step <= 1_000; step += 1) {
      policy.onDecodeStep(step, new Float32Array([weight]).subarray(0, 1));
    }
    // 1,000 additions of the float32 nearest 0.1, summed in float64.
    expect(policy.scoreOf(0)).toBeCloseTo(1_000 * weight, 9);
  });

  it("reuses the victim array, as the interface permits", () => {
    const policy = new H2o({ budget: 2, recentWindow: 1 });
    policy.onDecodeStep(1, new Float32Array([1]));
    policy.onDecodeStep(2, new Float32Array([0.5, 0.5]));
    const first = policy.evict(2);
    expect(first).toEqual([1]);

    policy.onDecodeStep(3, new Float32Array([0.5, 0.5]));
    const second = policy.evict(2);
    expect(second).toBe(first);
  });

  describe("on the canonical trace", () => {
    it("beats a plain sliding window on both metrics, at every budget", () => {
      for (const budget of [256, 512, 1_024]) {
        const window = kvCacheMetrics(
          runKvCacheTrace(new SlidingWindow({ budget }), SPEC, { budget }),
        );
        const h2o = kvCacheMetrics(runKvCacheTrace(new H2o({ budget }), SPEC, { budget }));

        expect(h2o.retainedAttentionMass).toBeGreaterThan(window.retainedAttentionMass);
        expect(h2o.heavyHitterRecall).toBeGreaterThan(window.heavyHitterRecall);
      }
    });

    it("finds more of the heavy hitters than StreamingLLM does", () => {
      // The claim the policy exists to make: reading attention locates the
      // important old tokens, which pinning the first four cannot.
      for (const budget of [256, 512, 1_024]) {
        const streaming = kvCacheMetrics(
          runKvCacheTrace(new StreamingLlm({ budget }), SPEC, { budget }),
        );
        const h2o = kvCacheMetrics(runKvCacheTrace(new H2o({ budget }), SPEC, { budget }));

        expect(h2o.heavyHitterRecall).toBeGreaterThan(streaming.heavyHitterRecall);
      }
    });

    it("retains LESS attention mass than StreamingLLM at the default window", () => {
      // Not a defect, and pinned here because it is the uncomfortable half of
      // the story: with only 32 recent positions protected, the 33rd- to 64th-
      // newest tokens must compete on cumulative score, and they lose to sinks
      // and to heavy hitters from earlier epochs that have been accumulating
      // for thousands of steps. The trace's recency component is 64 wide, so
      // that surrenders about a quarter of the local mass.
      const budget = 256;
      const streaming = kvCacheMetrics(
        runKvCacheTrace(new StreamingLlm({ budget }), SPEC, { budget }),
      );
      const h2o = kvCacheMetrics(runKvCacheTrace(new H2o({ budget }), SPEC, { budget }));

      expect(h2o.retainedAttentionMass).toBeLessThan(streaming.retainedAttentionMass);
    });

    it("overtakes StreamingLLM on both once its window covers the recency band", () => {
      // The other half, and the actionable one: widen recentWindow to the
      // model's local attention span and the deficit disappears. The gain
      // saturates exactly at the trace's span, which is what identifies the
      // cause as the window width rather than anything else.
      const budget = 256;
      const streaming = kvCacheMetrics(
        runKvCacheTrace(new StreamingLlm({ budget }), SPEC, { budget }),
      );
      const widened = kvCacheMetrics(
        runKvCacheTrace(
          new H2o({ budget, recentWindow: TRACE_LOCAL_SPAN }),
          SPEC,
          { budget },
        ),
      );

      expect(widened.retainedAttentionMass).toBeGreaterThan(streaming.retainedAttentionMass);
      expect(widened.heavyHitterRecall).toBeGreaterThan(streaming.heavyHitterRecall);

      // And it is genuinely saturation, not a trend: doubling the window again
      // buys almost nothing.
      const doubled = kvCacheMetrics(
        runKvCacheTrace(
          new H2o({ budget, recentWindow: TRACE_LOCAL_SPAN * 2 }),
          SPEC,
          { budget },
        ),
      );
      expect(doubled.retainedAttentionMass - widened.retainedAttentionMass).toBeLessThan(0.01);
    });

    it("keeps the attention sinks without being told about them", () => {
      // StreamingLLM pins positions 0-3 by rule. H2O has no such rule and
      // should arrive at the same place on the evidence, because a sink
      // collects attention on every single step.
      const policy = new H2o({ budget: 256 });
      runKvCacheTrace(policy, SPEC, { budget: 256 });

      for (const sink of [0, 1, 2, 3]) {
        expect(policy.scoreOf(sink)).toBeGreaterThan(0);
      }
    });
  });
});
