import { describe, expect, it } from "vitest";
import { runKvCacheTrace } from "./harness";
import type { KvCachePolicy } from "./interface";
import { kvCacheMetrics } from "./metrics";
import { KV_CACHE_TRACES, float32Bits, generateKvCacheTrace, hashKvCacheTrace } from "./traces";

const SPEC = KV_CACHE_TRACES["decode-4096"]!;

/** Keeps the most recent positions. The honest baseline. */
class SlidingWindow implements KvCachePolicy {
  // Position 0 is held from the start — the harness seeds the cache with it.
  readonly kept: number[] = [0];
  readonly seen: { pos: number; length: number; mass: number }[] = [];

  onDecodeStep(pos: number, attn: Float32Array | null): void {
    let mass = 0;
    if (attn !== null) for (const weight of attn) mass += weight;
    this.seen.push({ pos, length: attn?.length ?? -1, mass });
    this.kept.push(pos);
  }

  evict(budget: number): number[] {
    const drop = this.kept.slice(0, this.kept.length - budget);
    this.kept.splice(0, drop.length);
    return drop;
  }
}

function collect(maxSteps: number): Float32Array[] {
  return [...generateKvCacheTrace("decode-4096", maxSteps)];
}

describe("generateKvCacheTrace", () => {
  it("names the traces it knows when given one it does not", () => {
    expect(() => [...generateKvCacheTrace("nope")]).toThrow(/unknown kv-cache trace "nope"/);
  });

  it("yields one weight per earlier position", () => {
    const steps = collect(50);
    expect(steps).toHaveLength(50);
    steps.forEach((weights, index) => expect(weights.length).toBe(index + 1));
  });

  it("sums to one at every step, exactly enough for float32", () => {
    for (const weights of generateKvCacheTrace("decode-4096", 300)) {
      let total = 0;
      for (const weight of weights) total += weight;
      // Float32 weights summed in float64: a few ULPs of the largest term.
      expect(Math.abs(total - 1)).toBeLessThan(1e-6);
    }
  });

  it("never emits a zero weight, so no position is invisible", () => {
    // The noise component exists precisely so a policy cannot identify the
    // structure by testing for zero.
    for (const weights of generateKvCacheTrace("decode-4096", 200)) {
      for (const weight of weights) expect(weight).toBeGreaterThan(0);
    }
  });

  it("is reproducible from its seed alone", () => {
    const first = collect(140).map((weights) => Array.from(weights, float32Bits));
    const second = collect(140).map((weights) => Array.from(weights, float32Bits));
    expect(second).toEqual(first);
  });

  it("truncated is a prefix of the full run", () => {
    const short = collect(20).map((weights) => Array.from(weights, float32Bits));
    const long = collect(60)
      .slice(0, 20)
      .map((weights) => Array.from(weights, float32Bits));
    expect(short).toEqual(long);
  });

  describe("the mixture", () => {
    /** The four components, measured by where the mass falls. */
    function shares(weights: Float32Array): { sink: number; local: number; middle: number } {
      const t = weights.length;
      let sink = 0;
      let local = 0;
      let middle = 0;
      for (let i = 0; i < t; i += 1) {
        if (i < 4) sink += weights[i]!;
        else if (i >= t - 64) local += weights[i]!;
        else middle += weights[i]!;
      }
      return { sink, local, middle };
    }

    it("puts 0.15 on the sinks once there are four of them", () => {
      const late = shares(collect(600)[599]!);
      expect(late.sink).toBeCloseTo(0.15, 3);
    });

    it("folds the heavy mass into local below step 128", () => {
      // 0.55 + 0.25, less whatever noise lands in the window.
      const before = shares(collect(127)[126]!);
      expect(before.local).toBeGreaterThan(0.8);
      expect(before.middle).toBeLessThan(0.03);
    });

    it("switches the heavy component on at exactly step 128", () => {
      const steps = collect(128);
      const before = shares(steps[126]!);
      const after = shares(steps[127]!);

      // The step where the workload changes character, pinned because a port
      // that drew its heavy set one step early or late would diverge here.
      expect(before.middle).toBeLessThan(0.03);
      expect(after.middle).toBeGreaterThan(0.25);
      expect(after.local).toBeCloseTo(0.575, 2);
    });

    it("puts exactly 32 heavy hitters in the middle band, and none before they start", () => {
      // The middle band — clear of the sinks and the recency window — holds
      // only noise plus the heavy hitters, so counting elevated positions
      // there counts the heavy set. A heavy hitter drawn inside the local
      // window or the sinks would be missing from this count, and a duplicate
      // draw would collapse two into one.
      function elevatedInMiddleBand(weights: Float32Array): number {
        const t = weights.length;
        const noiseFloor = 0.05 / t;
        let elevated = 0;
        for (let i = 4; i < t - 64; i += 1) {
          if (weights[i]! > noiseFloor * 3) elevated += 1;
        }
        return elevated;
      }

      const steps = collect(600);
      expect(elevatedInMiddleBand(steps[599]!)).toBe(32);
      // Before step 128 there is no heavy component, so the band is pure noise.
      expect(elevatedInMiddleBand(steps[126]!)).toBe(0);
    });
  });

  it("hashes to a stable value", () => {
    // The parity artefact. If this changes, every port's expectation changes
    // with it and the change had better be deliberate.
    expect(hashKvCacheTrace("decode-4096", 200)).toBe(
      hashKvCacheTrace("decode-4096", 200),
    );
    expect(hashKvCacheTrace("decode-4096", 200)).toBeGreaterThan(0);
  });
});

describe("runKvCacheTrace", () => {
  it("shows the policy only what it still holds, in ascending order", () => {
    const policy = new SlidingWindow();
    runKvCacheTrace(policy, SPEC, { budget: 16, maxSteps: 40 });

    // The first call already shows one weight: position 0 starts kept, and at
    // step 1 it carries all the attention there is.
    expect(policy.seen[0]).toMatchObject({ pos: 1, length: 1, mass: 1 });
    // Before the budget binds, the policy sees everything: positions 0..5.
    expect(policy.seen[5]).toMatchObject({ pos: 6, length: 6 });
    // After it binds, never more than the budget.
    for (const entry of policy.seen) expect(entry.length).toBeLessThanOrEqual(16);
  });

  it("does not renormalise the weights it shows", () => {
    // The mass a policy sees falls below one by exactly what it has lost, which
    // is the signal `retainedAttentionMass` is built on.
    const policy = new SlidingWindow();
    runKvCacheTrace(policy, SPEC, { budget: 32, maxSteps: 200 });

    const late = policy.seen[policy.seen.length - 1]!;
    expect(late.mass).toBeLessThan(0.95);
    expect(late.mass).toBeGreaterThan(0.3);
  });

  it("asks for eviction exactly once per step past the budget", () => {
    const result = runKvCacheTrace(new SlidingWindow(), SPEC, { budget: 64, maxSteps: 300 });
    // After step t the cache holds t+1 positions (position 0 starts kept), so
    // the budget first binds at t = 64 and every later step evicts once.
    expect(result.evictionCalls).toBe(300 - 64 + 1);
    expect(result.evicted).toBe(300 - 64 + 1);
  });

  it("keeps all the mass when the budget never binds", () => {
    // The regression test for position 0 starting kept: if any position were
    // unreachable, even an unbounded budget could not retain everything.
    const result = runKvCacheTrace(new SlidingWindow(), SPEC, { budget: 4_096, maxSteps: 200 });
    expect(result.evictionCalls).toBe(0);
    expect(kvCacheMetrics(result).retainedAttentionMass).toBe(1);
  });

  it("rejects a policy that evicts what it does not hold", () => {
    // A position inside the sequence but not yet reached: valid as a number,
    // and not something the policy can possibly be holding at step 5.
    const policy: KvCachePolicy = {
      onDecodeStep: () => {},
      evict: () => [3_000],
    };
    expect(() => runKvCacheTrace(policy, SPEC, { budget: 4, maxSteps: 20 })).toThrow(
      /does not hold/,
    );
  });

  it("rejects a policy that does not free enough", () => {
    const policy: KvCachePolicy = {
      onDecodeStep: () => {},
      evict: () => [],
    };
    expect(() => runKvCacheTrace(policy, SPEC, { budget: 4, maxSteps: 20 })).toThrow(
      /must free enough/,
    );
  });

  it("rejects a nonsensical position", () => {
    const policy: KvCachePolicy = { onDecodeStep: () => {}, evict: () => [-1] };
    expect(() => runKvCacheTrace(policy, SPEC, { budget: 4, maxSteps: 20 })).toThrow(
      /not a valid position/,
    );
  });

  it("rejects a budget that is not a positive integer", () => {
    const policy = new SlidingWindow();
    expect(() => runKvCacheTrace(policy, SPEC, { budget: 0 })).toThrow(RangeError);
    expect(() => runKvCacheTrace(policy, SPEC, { budget: 1.5 })).toThrow(RangeError);
  });
});

describe("kvCacheMetrics", () => {
  it("improves with the budget, on both axes", () => {
    const measured = [128, 256, 512].map((budget) =>
      kvCacheMetrics(runKvCacheTrace(new SlidingWindow(), SPEC, { budget, maxSteps: 1_500 })),
    );

    for (let i = 1; i < measured.length; i += 1) {
      expect(measured[i]!.retainedAttentionMass).toBeGreaterThan(
        measured[i - 1]!.retainedAttentionMass,
      );
      expect(measured[i]!.heavyHitterRecall).toBeGreaterThan(measured[i - 1]!.heavyHitterRecall);
    }
  });

  it("credits a policy that keeps the sinks", () => {
    // Four positions carry 0.15 of the mass, so keeping them is worth about
    // eight points — which is the entire argument for StreamingLLM over a
    // plain window, and is measurable here.
    class Streaming extends SlidingWindow {
      override evict(budget: number): number[] {
        const keep = new Set<number>();
        for (const pos of this.kept) if (pos < 4) keep.add(pos);
        const rest = this.kept.filter((pos) => pos >= 4);
        for (const pos of rest.slice(Math.max(0, rest.length - (budget - keep.size)))) {
          keep.add(pos);
        }
        const drop = this.kept.filter((pos) => !keep.has(pos));
        this.kept.length = 0;
        this.kept.push(...[...keep].sort((a, b) => a - b));
        return drop;
      }
    }

    const window = kvCacheMetrics(
      runKvCacheTrace(new SlidingWindow(), SPEC, { budget: 256, maxSteps: 2_000 }),
    );
    const streaming = kvCacheMetrics(
      runKvCacheTrace(new Streaming(), SPEC, { budget: 256, maxSteps: 2_000 }),
    );

    expect(streaming.retainedAttentionMass - window.retainedAttentionMass).toBeGreaterThan(0.05);
    expect(streaming.heavyHitterRecall).toBeGreaterThan(window.heavyHitterRecall);
  });

  it("reports zeroes on an empty run rather than dividing by zero", () => {
    const result = runKvCacheTrace(new SlidingWindow(), SPEC, { budget: 8, maxSteps: 0 });
    expect(kvCacheMetrics(result)).toEqual({
      retainedAttentionMass: 0,
      heavyHitterRecall: 0,
      evictionCalls: 0,
    });
  });
});
