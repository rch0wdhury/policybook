import { describe, expect, it } from "vitest";
import { RATE_LIMITER_TRACES, generateRateLimiterTrace } from "./traces";

const IDS = Object.keys(RATE_LIMITER_TRACES);

describe("generateRateLimiterTrace", () => {
  it("names the traces it knows when given one it does not", () => {
    expect(() => generateRateLimiterTrace("nope")).toThrow(/unknown rate-limiter trace "nope"/);
    expect(() => generateRateLimiterTrace("nope")).toThrow(/steady, bursty, many-keys/);
  });

  it.each(IDS)("%s is reproducible from its seed alone", (id) => {
    const first = generateRateLimiterTrace(id);
    const second = generateRateLimiterTrace(id);
    expect(Array.from(second.times)).toEqual(Array.from(first.times));
    expect(Array.from(second.keys)).toEqual(Array.from(first.keys));
  });

  it.each(IDS)("%s is non-decreasing in time and inside its window", (id) => {
    const spec = RATE_LIMITER_TRACES[id]!;
    const { times } = generateRateLimiterTrace(id);
    for (let index = 1; index < times.length; index += 1) {
      expect(times[index]!).toBeGreaterThanOrEqual(times[index - 1]!);
    }
    expect(times[times.length - 1]!).toBeLessThan(spec.durationMs);
  });

  it.each(IDS)("%s stays inside its key universe", (id) => {
    const spec = RATE_LIMITER_TRACES[id]!;
    const { keys } = generateRateLimiterTrace(id);
    for (const key of keys) expect(key).toBeLessThan(spec.keyUniverse);
  });

  it.each(IDS)("%s emits at most one arrival per millisecond", (id) => {
    // One Bernoulli trial per millisecond is the whole generator; two arrivals
    // sharing a timestamp would mean the loop had drifted from that.
    const { times } = generateRateLimiterTrace(id);
    for (let index = 1; index < times.length; index += 1) {
      expect(times[index]!).toBeGreaterThan(times[index - 1]!);
    }
  });

  it.each(IDS)("%s truncated is a prefix of %s in full", (id) => {
    const full = generateRateLimiterTrace(id, 500);
    const short = generateRateLimiterTrace(id, 100);
    expect(Array.from(short.times)).toEqual(Array.from(full.times).slice(0, 100));
    expect(Array.from(short.keys)).toEqual(Array.from(full.keys).slice(0, 100));
  });

  describe("steady", () => {
    it("arrives at about 90 per second, just under the 100/s reference", () => {
      const { times } = generateRateLimiterTrace("steady");
      const perSecond = (times.length / 60_000) * 1_000;
      expect(perSecond).toBeGreaterThan(85);
      expect(perSecond).toBeLessThan(95);
    });

    it("names one key only", () => {
      const { keys } = generateRateLimiterTrace("steady");
      expect(new Set(keys).size).toBe(1);
      expect(keys[0]).toBe(0);
    });
  });

  describe("bursty", () => {
    it("puts every arrival inside an ON phase", () => {
      // The OFF phase consumes no draw and can produce no arrival; an arrival
      // at t mod 2000 >= 200 would mean the cycle arithmetic had slipped.
      const { times } = generateRateLimiterTrace("bursty");
      for (const t of times) expect(t % 2_000).toBeLessThan(200);
    });

    it("delivers about a hundred arrivals per burst", () => {
      const { times } = generateRateLimiterTrace("bursty");
      const bursts = new Map<number, number>();
      for (const t of times) {
        const cycle = Math.floor(t / 2_000);
        bursts.set(cycle, (bursts.get(cycle) ?? 0) + 1);
      }
      expect(bursts.size).toBe(30);
      for (const count of bursts.values()) {
        expect(count).toBeGreaterThan(70);
        expect(count).toBeLessThan(130);
      }
    });

    it("averages about half the reference rate over the whole minute", () => {
      // 200 ms of 500/s in every 2,000 ms is 50/s sustained. A policy that can
      // save up during the silence should therefore pass nearly everything —
      // which is the comparison the trace exists to set up.
      const { times } = generateRateLimiterTrace("bursty");
      const perSecond = (times.length / 60_000) * 1_000;
      expect(perSecond).toBeGreaterThan(40);
      expect(perSecond).toBeLessThan(60);
    });
  });

  describe("many-keys", () => {
    it("is skewed, with key 0 the most popular", () => {
      const { keys } = generateRateLimiterTrace("many-keys");
      const counts = new Map<number, number>();
      for (const key of keys) counts.set(key, (counts.get(key) ?? 0) + 1);

      const ranked = [...counts.entries()].sort((a, b) => b[1] - a[1]);
      expect(ranked[0]![0]).toBe(0);
      // Zipf alpha 1.0 over 10,000 keys puts about a tenth of the traffic on
      // the single most popular key.
      expect(ranked[0]![1] / keys.length).toBeGreaterThan(0.05);
    });

    it("reaches a large part of the keyspace", () => {
      const { keys } = generateRateLimiterTrace("many-keys");
      expect(new Set(keys).size).toBeGreaterThan(2_000);
    });
  });
});
