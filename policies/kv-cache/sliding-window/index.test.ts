import { describe, expect, it } from "vitest";
import { runKvCacheTrace } from "../../../packages/core/src/domains/kv-cache/harness";
import { kvCacheMetrics } from "../../../packages/core/src/domains/kv-cache/metrics";
import { KV_CACHE_TRACES } from "../../../packages/core/src/domains/kv-cache/traces";
import SlidingWindow from "./index";

const SPEC = KV_CACHE_TRACES["decode-4096"]!;

describe("SlidingWindow", () => {
  it("rejects a budget that is not a positive integer", () => {
    expect(() => new SlidingWindow({ budget: 0 })).toThrow(RangeError);
    expect(() => new SlidingWindow({ budget: -1 })).toThrow(RangeError);
    expect(() => new SlidingWindow({ budget: 2.5 })).toThrow(RangeError);
  });

  it("starts holding position 0", () => {
    // The token that exists before decoding begins. A policy that started
    // empty would be permanently one position behind the harness.
    expect(new SlidingWindow({ budget: 4 }).keptCount()).toBe(1);
  });

  it("says so when the budget it was built with is not the one it is run at", () => {
    // Silently growing past the budget would defeat the point of the domain,
    // and silently dropping would corrupt the harness's accounting.
    const policy = new SlidingWindow({ budget: 2 });
    policy.onDecodeStep(1);
    policy.onDecodeStep(2);
    expect(() => policy.onDecodeStep(3)).toThrow(/budget must match/);
  });

  it("reuses the victim array, as the interface permits", () => {
    const policy = new SlidingWindow({ budget: 2 });
    policy.onDecodeStep(1);
    policy.onDecodeStep(2);
    const first = policy.evict(2);
    expect(first).toEqual([0]);

    policy.onDecodeStep(3);
    const second = policy.evict(2);
    // Same array object, new contents — which is exactly why the interface
    // tells callers to copy if they want to keep it.
    expect(second).toBe(first);
    expect(second).toEqual([1]);
  });

  describe("on the canonical trace", () => {
    it("holds exactly the last `budget` positions at every step", () => {
      // The defining property, checked against the harness rather than
      // against the policy's own bookkeeping.
      const policy = new SlidingWindow({ budget: 64 });
      const result = runKvCacheTrace(policy, SPEC, { budget: 64, maxSteps: 500 });

      expect(policy.keptCount()).toBe(64);
      // The budget first binds at t = 64, since the cache starts with one.
      expect(result.evictionCalls).toBe(500 - 64 + 1);
      expect(result.evicted).toBe(result.evictionCalls);
    });

    it("retains more attention as the budget grows", () => {
      const measured = [128, 256, 512].map((budget) =>
        kvCacheMetrics(
          runKvCacheTrace(new SlidingWindow({ budget }), SPEC, { budget, maxSteps: 1_500 }),
        ),
      );

      for (let i = 1; i < measured.length; i += 1) {
        expect(measured[i]!.retainedAttentionMass).toBeGreaterThan(
          measured[i - 1]!.retainedAttentionMass,
        );
      }
    });

    it("loses the attention sinks almost immediately", () => {
      // The characteristic failure, and the reason streaming-llm exists. By
      // the time the budget has bound, position 0 is long gone.
      const policy = new SlidingWindow({ budget: 32 });
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
        { budget: 32, maxSteps: 200 },
      );

      expect(dropped[0]).toBe(0);
      expect(dropped.slice(0, 4)).toEqual([0, 1, 2, 3]);
    });
  });
});
