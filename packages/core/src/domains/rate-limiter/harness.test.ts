import { describe, expect, it } from "vitest";
import { runRateLimiterTrace } from "./harness";
import type { RateLimiterPolicy } from "./interface";
import { rateLimiterMetrics } from "./metrics";
import type { RateLimiterTrace } from "./traces";

/** A limiter that accepts everything, so the harness is the only thing tested. */
class AlwaysAllow implements RateLimiterPolicy {
  readonly calls: { key: number; cost: number; now: number }[] = [];

  allow(key: number, cost: number, now: number): boolean {
    this.calls.push({ key, cost, now });
    return true;
  }
}

/** Accepts the first `budget` requests it ever sees, then refuses. */
class AllowFirst implements RateLimiterPolicy {
  private remaining: number;

  constructor(budget: number) {
    this.remaining = budget;
  }

  allow(): boolean {
    if (this.remaining === 0) return false;
    this.remaining -= 1;
    return true;
  }
}

/** Accepts only even keys — a fixed, knowable unfairness to measure. */
class EvenKeysOnly implements RateLimiterPolicy {
  allow(key: number): boolean {
    return key % 2 === 0;
  }
}

function trace(times: number[], keys: number[]): RateLimiterTrace {
  return { times: Uint32Array.from(times), keys: Uint32Array.from(keys) };
}

describe("runRateLimiterTrace", () => {
  it("passes each arrival to the policy with its time and cost", () => {
    const policy = new AlwaysAllow();
    const result = runRateLimiterTrace(policy, trace([0, 5, 5, 9], [0, 1, 0, 1]), {
      keyUniverse: 2,
    });

    expect(policy.calls).toEqual([
      { key: 0, cost: 1, now: 0 },
      { key: 1, cost: 1, now: 5 },
      { key: 0, cost: 1, now: 5 },
      { key: 1, cost: 1, now: 9 },
    ]);
    expect(result).toMatchObject({ events: 4, accepted: 4, denied: 0 });
  });

  it("counts accepts and denials", () => {
    const result = runRateLimiterTrace(new AllowFirst(2), trace([0, 1, 2, 3], [0, 0, 0, 0]), {
      keyUniverse: 1,
    });

    expect(result.accepted).toBe(2);
    expect(result.denied).toBe(2);
    expect(rateLimiterMetrics(result).acceptRate).toBe(0.5);
  });

  it("forwards a non-default cost", () => {
    const policy = new AlwaysAllow();
    runRateLimiterTrace(policy, trace([0], [0]), { keyUniverse: 1, cost: 7 });
    expect(policy.calls[0]!.cost).toBe(7);
  });

  describe("maxBurst100ms", () => {
    it("is the largest count in any window, not the count in fixed windows", () => {
      // Four accepts at 50, 90, 130, 170. No aligned 100 ms block holds more
      // than two, but the window [90, 189] holds three — which is the number a
      // downstream service would actually feel.
      const result = runRateLimiterTrace(
        new AlwaysAllow(),
        trace([50, 90, 130, 170], [0, 0, 0, 0]),
        { keyUniverse: 1 },
      );
      expect(result.maxBurst100ms).toBe(3);
    });

    it("treats the window as inclusive at both ends", () => {
      // 0 and 99 are 99 ms apart and share a window; 0 and 100 do not.
      expect(
        runRateLimiterTrace(new AlwaysAllow(), trace([0, 99], [0, 0]), { keyUniverse: 1 })
          .maxBurst100ms,
      ).toBe(2);
      expect(
        runRateLimiterTrace(new AlwaysAllow(), trace([0, 100], [0, 0]), { keyUniverse: 1 })
          .maxBurst100ms,
      ).toBe(1);
    });

    it("counts only accepts", () => {
      const result = runRateLimiterTrace(new AllowFirst(1), trace([0, 1, 2], [0, 0, 0]), {
        keyUniverse: 1,
      });
      expect(result.maxBurst100ms).toBe(1);
    });
  });

  describe("fairness", () => {
    it("is one when every key is served equally", () => {
      const result = runRateLimiterTrace(
        new AlwaysAllow(),
        trace([0, 1, 2, 3], [0, 1, 0, 1]),
        { keyUniverse: 2 },
      );
      expect(rateLimiterMetrics(result).jainFairness).toBe(1);
    });

    it("is 1/n when one key of n takes everything", () => {
      // Two keys arrive, only key 0 is served: (2)^2 / (2 * 4) = 0.5.
      const result = runRateLimiterTrace(
        new EvenKeysOnly(),
        trace([0, 1, 2, 3], [0, 1, 0, 1]),
        { keyUniverse: 2 },
      );
      expect(result.keysSeen).toBe(2);
      expect(rateLimiterMetrics(result).jainFairness).toBe(0.5);
    });

    it("ignores keys that never arrived", () => {
      // A universe of 1,000 keys where only two ever appear must not be scored
      // as though the policy starved the other 998.
      const result = runRateLimiterTrace(
        new AlwaysAllow(),
        trace([0, 1], [0, 999]),
        { keyUniverse: 1_000 },
      );
      expect(result.keysSeen).toBe(2);
      expect(rateLimiterMetrics(result).jainFairness).toBe(1);
    });

    it("is null on a single-key trace, where it means nothing", () => {
      const result = runRateLimiterTrace(new AlwaysAllow(), trace([0, 1], [0, 0]), {
        keyUniverse: 1,
      });
      expect(rateLimiterMetrics(result).jainFairness).toBeNull();
    });
  });

  describe("stateSize", () => {
    it("reports the high-water mark, not the final value", () => {
      let size = 0;
      const policy: RateLimiterPolicy = {
        allow: () => true,
        stateSize: () => size,
      };
      const growThenShrink = [1, 5, 3, 2];
      let index = 0;
      const wrapped: RateLimiterPolicy = {
        allow: (key, cost, now) => {
          size = growThenShrink[index] ?? 0;
          index += 1;
          return policy.allow(key, cost, now);
        },
        stateSize: () => size,
      };

      const result = runRateLimiterTrace(wrapped, trace([0, 1, 2, 3], [0, 0, 0, 0]), {
        keyUniverse: 1,
      });
      expect(result.entriesTracked).toBe(5);
    });

    it("is null when the policy does not report it", () => {
      const result = runRateLimiterTrace(new AlwaysAllow(), trace([0], [0]), { keyUniverse: 1 });
      expect(result.entriesTracked).toBeNull();
    });

    it("rejects a nonsensical value rather than reporting it", () => {
      const policy: RateLimiterPolicy = { allow: () => true, stateSize: () => -1 };
      expect(() =>
        runRateLimiterTrace(policy, trace([0], [0]), { keyUniverse: 1 }),
      ).toThrow(/non-negative integer/);
    });
  });

  describe("contract violations", () => {
    it("rejects a key outside the universe", () => {
      expect(() =>
        runRateLimiterTrace(new AlwaysAllow(), trace([0], [9]), { keyUniverse: 2 }),
      ).toThrow(/outside the key universe/);
    });

    it("rejects a trace that goes backwards in time", () => {
      expect(() =>
        runRateLimiterTrace(new AlwaysAllow(), trace([5, 4], [0, 0]), { keyUniverse: 1 }),
      ).toThrow(/non-decreasing/);
    });

    it("rejects mismatched times and keys", () => {
      const bad = { times: Uint32Array.from([0, 1]), keys: Uint32Array.from([0]) };
      expect(() => runRateLimiterTrace(new AlwaysAllow(), bad, { keyUniverse: 1 })).toThrow(
        /2 times but 1 keys/,
      );
    });

    it("rejects a bad key universe", () => {
      expect(() =>
        runRateLimiterTrace(new AlwaysAllow(), trace([0], [0]), { keyUniverse: 0 }),
      ).toThrow(/positive integer/);
    });
  });

  it("reports zero accept rate on an empty trace rather than dividing by zero", () => {
    const result = runRateLimiterTrace(new AlwaysAllow(), trace([], []), { keyUniverse: 1 });
    expect(rateLimiterMetrics(result)).toMatchObject({ acceptRate: 0, maxBurst100ms: 0 });
  });
});
