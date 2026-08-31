import { describe, expect, it } from "vitest";
import { CACHE_TRACES, generateCacheTrace } from "./traces";

/** Counts occurrences of each key. */
function histogram(trace: Uint32Array): Map<number, number> {
  const counts = new Map<number, number>();
  for (const key of trace) counts.set(key, (counts.get(key) ?? 0) + 1);
  return counts;
}

/** The key seen most often in `trace[from..to)`. */
function mode(trace: Uint32Array, from: number, to: number): number {
  const counts = histogram(trace.subarray(from, to));
  let best = -1;
  let bestCount = -1;
  for (const [key, count] of counts) {
    if (count > bestCount) {
      best = key;
      bestCount = count;
    }
  }
  return best;
}

describe("cache traces", () => {
  it("rejects an unknown trace by name, listing the real ones", () => {
    expect(() => generateCacheTrace("nope")).toThrow(/unknown cache trace "nope"/);
    expect(() => generateCacheTrace("nope")).toThrow(/zipf-1\.0-100k/);
  });

  for (const spec of Object.values(CACHE_TRACES)) {
    describe(spec.id, () => {
      // The million-event trace is generated once and shared across its
      // assertions; regenerating it per test would dominate the suite.
      const trace = generateCacheTrace(spec.id);

      it("has the declared length", () => {
        expect(trace.length).toBe(spec.events);
      });

      it("stays inside the declared key universe", () => {
        let max = 0;
        for (const key of trace) if (key > max) max = key;
        expect(max).toBeLessThan(spec.keyUniverse);
      });

      it("is deterministic", () => {
        const again = generateCacheTrace(spec.id, 5_000);
        expect(Array.from(again)).toEqual(Array.from(trace.subarray(0, 5_000)));
      });

      it("truncates to a genuine prefix", () => {
        // The site runs a reduced-length trace; a short run must be the start
        // of the real one, not a different workload.
        const short = generateCacheTrace(spec.id, 1_000);
        expect(short.length).toBe(1_000);
        expect(Array.from(short)).toEqual(Array.from(trace.subarray(0, 1_000)));
      });
    });
  }

  it("zipf-1.0-100k is skewed with key 0 most popular", () => {
    const trace = generateCacheTrace("zipf-1.0-100k");
    const counts = histogram(trace);
    expect(mode(trace, 0, trace.length)).toBe(0);
    // Zipf alpha 1: key 0 should be roughly twice key 1.
    const ratio = (counts.get(0) ?? 0) / (counts.get(1) ?? 1);
    expect(ratio).toBeGreaterThan(1.7);
    expect(ratio).toBeLessThan(2.3);
  });

  it("zipf-0.75-1m is flatter than zipf-1.0", () => {
    const flat = histogram(generateCacheTrace("zipf-0.75-1m", 200_000));
    const steep = histogram(generateCacheTrace("zipf-1.0-100k"));
    const flatRatio = (flat.get(0) ?? 0) / (flat.get(1) ?? 1);
    const steepRatio = (steep.get(0) ?? 0) / (steep.get(1) ?? 1);
    expect(flatRatio).toBeLessThan(steepRatio);
  });

  describe("scan-heavy", () => {
    const spec = CACHE_TRACES["scan-heavy"]!;
    const trace = generateCacheTrace("scan-heavy");

    it("is 100,000 zipf events plus four bursts of 2,000", () => {
      expect(trace.length).toBe(108_000);
    });

    it("puts the first scan immediately after 20,000 accesses", () => {
      // Everything before the scan is working-set traffic.
      for (let index = 0; index < 20_000; index += 1) {
        expect(trace[index]!).toBeLessThan(spec.keyspace);
      }
      // Then 2,000 consecutive fresh keys, starting at the keyspace boundary.
      for (let offset = 0; offset < 2_000; offset += 1) {
        expect(trace[20_000 + offset]!).toBe(10_000 + offset);
      }
    });

    it("touches every scan key exactly once", () => {
      // This is what makes a scan a scan: the keys are never reused, so any
      // policy that caches them has wasted the space.
      const counts = histogram(trace);
      let scanKeys = 0;
      for (const [key, count] of counts) {
        if (key >= spec.keyspace) {
          expect(count).toBe(1);
          scanKeys += 1;
        }
      }
      expect(scanKeys).toBe(8_000);
    });

    it("keeps scan keys clear of the working set", () => {
      const counts = histogram(trace);
      for (const key of counts.keys()) {
        if (key >= spec.keyspace) {
          expect(key).toBeGreaterThanOrEqual(10_000);
          expect(key).toBeLessThan(18_000);
        }
      }
    });
  });

  describe("shifting-popularity", () => {
    const trace = generateCacheTrace("shifting-popularity");

    it("rotates the popular key every 25,000 accesses", () => {
      // The hot key moves by 2,500 each window; a policy that cannot forget
      // keeps holding the previous window's winners.
      expect(mode(trace, 0, 25_000)).toBe(0);
      expect(mode(trace, 25_000, 50_000)).toBe(2_500);
      expect(mode(trace, 50_000, 75_000)).toBe(5_000);
      expect(mode(trace, 75_000, 100_000)).toBe(7_500);
    });

    it("keeps the same skew in every window", () => {
      // Only the labels rotate; the distribution's shape is unchanged.
      const first = histogram(trace.subarray(0, 25_000));
      const second = histogram(trace.subarray(25_000, 50_000));
      const firstTop = first.get(0) ?? 0;
      const secondTop = second.get(2_500) ?? 0;
      expect(Math.abs(firstTop - secondTop) / firstTop).toBeLessThan(0.15);
    });
  });

  it("has not silently changed", () => {
    // A regression guard, not a specification: these are the first eight keys
    // of each trace. If one of these changes, every committed benchmark number
    // and every port's parity test is invalidated, so the change had better be
    // deliberate.
    const prefixes: Record<string, number[]> = {};
    for (const id of Object.keys(CACHE_TRACES)) {
      prefixes[id] = Array.from(generateCacheTrace(id, 8));
    }
    expect(prefixes).toEqual({
      "zipf-1.0-100k": [2, 2314, 0, 3, 104, 3135, 692, 41],
      "zipf-0.75-1m": [2725, 19446, 3708, 12707, 3, 52230, 80168, 8329],
      "scan-heavy": [4852, 2939, 209, 4231, 3267, 19, 6, 59],
      "shifting-popularity": [2, 2, 10, 377, 2770, 53, 5162, 0],
    });
  });
});
