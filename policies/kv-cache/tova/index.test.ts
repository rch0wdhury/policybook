import { describe, expect, it } from "vitest";
import { runKvCacheTrace } from "../../../packages/core/src/domains/kv-cache/harness";
import { kvCacheMetrics } from "../../../packages/core/src/domains/kv-cache/metrics";
import { KV_CACHE_TRACES } from "../../../packages/core/src/domains/kv-cache/traces";
import H2o from "../h2o/index";
import SlidingWindow from "../sliding-window/index";
import StreamingLlm from "../streaming-llm/index";
import Tova from "./index";

const SPEC = KV_CACHE_TRACES["decode-4096"]!;

/** The width of the trace's recency component; see the domain's TRACES.md. */
const TRACE_LOCAL_SPAN = 64;
/** The last decode step of a 4,096-token sequence. */
const LAST_STEP = 4_095;

describe("Tova", () => {
  it("rejects a budget that is not a positive integer", () => {
    expect(() => new Tova({ budget: 0 })).toThrow(RangeError);
    expect(() => new Tova({ budget: 2.5 })).toThrow(RangeError);
  });

  it("starts holding position 0, unobserved", () => {
    const policy = new Tova({ budget: 8 });
    expect(policy.keptCount()).toBe(1);
    expect(policy.lastAttentionOf(0)).toBe(-1);
  });

  it("replaces the record rather than accumulating it", () => {
    // The entire difference from h2o, stated directly.
    const policy = new Tova({ budget: 8 });
    policy.onDecodeStep(1, Float32Array.from([1.0]));
    expect(policy.lastAttentionOf(0)).toBe(1.0);
    policy.onDecodeStep(2, Float32Array.from([0.25, 0.75]));
    expect(policy.lastAttentionOf(0)).toBe(0.25);
    policy.onDecodeStep(3, Float32Array.from([0.125, 0.5, 0.375]));
    expect(policy.lastAttentionOf(0)).toBe(0.125);
  });

  it("never evicts a position nothing has attended to yet", () => {
    // Ranking a position on evidence that does not exist would evict every
    // token on the step it was generated, and the cache would never advance.
    const policy = new Tova({ budget: 2 });
    policy.onDecodeStep(1, Float32Array.from([1.0]));
    policy.onDecodeStep(2, Float32Array.from([0.5, 0.5]));
    expect(policy.lastAttentionOf(2)).toBe(-1);
    expect(policy.evict(2)).toEqual([0]);
  });

  describe("on the canonical trace", () => {
    it("beats a plain sliding window on both metrics, at every budget", () => {
      for (const budget of [256, 512, 1_024]) {
        const window = kvCacheMetrics(
          runKvCacheTrace(new SlidingWindow({ budget }), SPEC, { budget }),
        );
        const tova = kvCacheMetrics(runKvCacheTrace(new Tova({ budget }), SPEC, { budget }));

        expect(tova.retainedAttentionMass).toBeGreaterThan(window.retainedAttentionMass);
        expect(tova.heavyHitterRecall).toBeGreaterThan(window.heavyHitterRecall);
      }
    });

    it("beats StreamingLLM on both metrics, at every budget", () => {
      // Which h2o does not manage on retained mass — see the comparison below.
      for (const budget of [256, 512, 1_024]) {
        const streaming = kvCacheMetrics(
          runKvCacheTrace(new StreamingLlm({ budget }), SPEC, { budget }),
        );
        const tova = kvCacheMetrics(runKvCacheTrace(new Tova({ budget }), SPEC, { budget }));

        expect(tova.retainedAttentionMass).toBeGreaterThan(streaming.retainedAttentionMass);
        expect(tova.heavyHitterRecall).toBeGreaterThan(streaming.heavyHitterRecall);
      }
    });

    it("keeps the whole recency band without a recency rule", () => {
      // The paper's argument, checked rather than repeated: recency is already
      // in the signal, because recent tokens attract high attention *now*. This
      // policy has no recentWindow parameter at all and still holds every one
      // of the 64 positions the trace's local component covers — which is
      // exactly the band h2o's fixed 32-position window leaves half exposed.
      const budget = 256;
      const policy = new Tova({ budget });
      runKvCacheTrace(policy, SPEC, { budget });

      let held = 0;
      for (let pos = LAST_STEP - TRACE_LOCAL_SPAN; pos < LAST_STEP; pos += 1) {
        if (policy.lastAttentionOf(pos) !== -1) held += 1;
      }
      expect(held).toBe(TRACE_LOCAL_SPAN);
    });

    it("keeps the attention sinks without a sink rule", () => {
      // The other half of the same argument. StreamingLLM pins positions 0-3 by
      // fiat; this policy arrives at them because a sink attracts attention on
      // every single step, which is what the current-step weight reports.
      const budget = 256;
      const policy = new Tova({ budget });
      runKvCacheTrace(policy, SPEC, { budget });

      for (const sink of [0, 1, 2, 3]) {
        expect(policy.lastAttentionOf(sink)).not.toBe(-1);
      }
    });

    it("trades recall for mass against h2o", () => {
      // The honest summary of the two. Accumulating finds more of the heavy
      // hitters; reading only the current step protects recency far better,
      // because it sizes the protection from the data instead of from a
      // parameter. Neither dominates, and the benchmark table should not be
      // read as though one did.
      for (const budget of [256, 512, 1_024]) {
        const h2o = kvCacheMetrics(runKvCacheTrace(new H2o({ budget }), SPEC, { budget }));
        const tova = kvCacheMetrics(runKvCacheTrace(new Tova({ budget }), SPEC, { budget }));

        expect(tova.retainedAttentionMass).toBeGreaterThan(h2o.retainedAttentionMass);
        expect(tova.heavyHitterRecall).toBeLessThan(h2o.heavyHitterRecall);
      }
    });
  });
});
