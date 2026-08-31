import { describe, expect, it } from "vitest";
import { runKvCacheTrace } from "../../../packages/core/src/domains/kv-cache/harness";
import { kvCacheMetrics } from "../../../packages/core/src/domains/kv-cache/metrics";
import { KV_CACHE_TRACES } from "../../../packages/core/src/domains/kv-cache/traces";
import SlidingWindow from "../sliding-window/index";
import StreamingLlm from "./index";

const SPEC = KV_CACHE_TRACES["decode-4096"]!;

describe("StreamingLlm", () => {
  it("rejects parameters that leave no recency window", () => {
    expect(() => new StreamingLlm({ budget: 4, sinks: 4 })).toThrow(/must be smaller than/);
    expect(() => new StreamingLlm({ budget: 4, sinks: 9 })).toThrow(/must be smaller than/);
    expect(() => new StreamingLlm({ budget: 0 })).toThrow(RangeError);
    expect(() => new StreamingLlm({ budget: 8, sinks: -1 })).toThrow(RangeError);
  });

  it("counts position 0 as a sink from the outset", () => {
    const policy = new StreamingLlm({ budget: 8, sinks: 4 });
    expect(policy.keptCount()).toBe(1);
    expect(policy.sinkCount()).toBe(1);
  });

  it("puts position 0 in the window when no sinks are configured", () => {
    const policy = new StreamingLlm({ budget: 8, sinks: 0 });
    expect(policy.keptCount()).toBe(1);
    expect(policy.sinkCount()).toBe(0);
  });

  describe("on the canonical trace", () => {
    it("never evicts a sink", () => {
      const policy = new StreamingLlm({ budget: 64, sinks: 4 });
      const dropped: number[] = [];

      runKvCacheTrace(
        {
          onDecodeStep: (pos) => policy.onDecodeStep(pos),
          evict: (budget) => {
            const victims = policy.evict(budget);
            dropped.push(...victims);
            return victims;
          },
        },
        SPEC,
        { budget: 64, maxSteps: 1_000 },
      );

      expect(dropped.length).toBeGreaterThan(900);
      expect(dropped.some((pos) => pos < 4)).toBe(false);
      expect(policy.sinkCount()).toBe(4);
    });

    it("beats a sliding window on both metrics, at every budget", () => {
      // The claim the policy exists to make, measured rather than asserted.
      for (const budget of [256, 512, 1_024]) {
        const window = kvCacheMetrics(
          runKvCacheTrace(new SlidingWindow({ budget }), SPEC, { budget }),
        );
        const streaming = kvCacheMetrics(
          runKvCacheTrace(new StreamingLlm({ budget }), SPEC, { budget }),
        );

        expect(streaming.retainedAttentionMass).toBeGreaterThan(window.retainedAttentionMass);
        expect(streaming.heavyHitterRecall).toBeGreaterThan(window.heavyHitterRecall);
      }
    });

    it("recovers the sink mass, which is what the gap is made of", () => {
      // The four sinks carry 0.15 of the attention between them. A window that
      // has dropped all four and a policy that kept all four should differ by
      // about that much — and if the gap were much larger or smaller, one of
      // the two policies would be doing something other than advertised.
      const budget = 256;
      const window = kvCacheMetrics(
        runKvCacheTrace(new SlidingWindow({ budget }), SPEC, { budget }),
      );
      const streaming = kvCacheMetrics(
        runKvCacheTrace(new StreamingLlm({ budget }), SPEC, { budget }),
      );

      const gap = streaming.retainedAttentionMass - window.retainedAttentionMass;
      expect(gap).toBeGreaterThan(0.12);
      expect(gap).toBeLessThan(0.16);
    });

    it("is a plain sliding window when sinks is zero", () => {
      // The honest statement of what the sink count buys: with none, the two
      // policies must agree exactly, not merely closely.
      const budget = 128;
      const window = kvCacheMetrics(
        runKvCacheTrace(new SlidingWindow({ budget }), SPEC, { budget, maxSteps: 800 }),
      );
      const streaming = kvCacheMetrics(
        runKvCacheTrace(
          new StreamingLlm({ budget, sinks: 0 }),
          SPEC,
          { budget, maxSteps: 800 },
        ),
      );

      expect(streaming).toEqual(window);
    });
  });
});
