import { describe, expect, it } from "vitest";
import { Rng, mix32 } from "./rng";
import vectors from "./rng.vectors.json";

const TWO_POW_32 = 4294967296;

describe("Rng reference vectors", () => {
  // These are the vectors every port is measured against. If one of these
  // fails, the generator changed and every language is now wrong together.
  for (const entry of vectors.seeds) {
    describe(`seed ${entry.seed}`, () => {
      it(`reproduces ${entry.nextU32.length} nextU32 values`, () => {
        const rng = new Rng(entry.seed);
        const actual = entry.nextU32.map(() => rng.nextU32());
        expect(actual).toEqual(entry.nextU32);
      });

      it(`reproduces ${entry.nextFloat.length} nextFloat values exactly`, () => {
        const rng = new Rng(entry.seed);
        const actual = entry.nextFloat.map(() => String(rng.nextFloat()));
        expect(actual).toEqual(entry.nextFloat);
      });

      for (const testCase of entry.nextInt) {
        it(`reproduces nextInt(${testCase.bound})`, () => {
          const rng = new Rng(entry.seed);
          const actual = testCase.values.map(() => rng.nextInt(testCase.bound));
          expect(actual).toEqual(testCase.values);
        });
      }
    });
  }

  it("reproduces the mix32 key hash", () => {
    for (const pair of vectors.mix32) {
      expect(mix32(pair.input)).toBe(pair.output);
    }
  });
});

describe("Rng contract", () => {
  it("is deterministic: equal seeds give equal streams", () => {
    const a = new Rng(42);
    const b = new Rng(42);
    for (let i = 0; i < 1000; i += 1) expect(a.nextU32()).toBe(b.nextU32());
  });

  it("gives different streams for different seeds", () => {
    const a = new Rng(1);
    const b = new Rng(2);
    const streamA = Array.from({ length: 64 }, () => a.nextU32());
    const streamB = Array.from({ length: 64 }, () => b.nextU32());
    expect(streamA).not.toEqual(streamB);
  });

  it("works for seed 0 and does not get stuck", () => {
    const rng = new Rng(0);
    const values = new Set(Array.from({ length: 1000 }, () => rng.nextU32()));
    expect(values.size).toBeGreaterThan(990);
  });

  it("keeps nextU32 inside [0, 2^32)", () => {
    const rng = new Rng(7);
    for (let i = 0; i < 10_000; i += 1) {
      const value = rng.nextU32();
      expect(Number.isInteger(value)).toBe(true);
      expect(value).toBeGreaterThanOrEqual(0);
      expect(value).toBeLessThan(TWO_POW_32);
    }
  });

  it("keeps nextFloat inside [0, 1)", () => {
    const rng = new Rng(9);
    for (let i = 0; i < 10_000; i += 1) {
      const value = rng.nextFloat();
      expect(value).toBeGreaterThanOrEqual(0);
      expect(value).toBeLessThan(1);
    }
  });

  it("rejects a bound that is not a positive integer", () => {
    const rng = new Rng(1);
    expect(() => rng.nextInt(0)).toThrow(RangeError);
    expect(() => rng.nextInt(-5)).toThrow(RangeError);
    expect(() => rng.nextInt(2.5)).toThrow(RangeError);
    expect(() => rng.nextInt(TWO_POW_32 + 1)).toThrow(RangeError);
    // The error names the offending value, so a caller can see what happened.
    expect(() => rng.nextInt(0)).toThrow(/received 0/);
  });

  it("returns 0 for a bound of 1", () => {
    const rng = new Rng(3);
    for (let i = 0; i < 100; i += 1) expect(rng.nextInt(1)).toBe(0);
  });

  it("keeps nextInt inside [0, bound)", () => {
    const rng = new Rng(11);
    for (const bound of [2, 3, 7, 100, 65_537]) {
      for (let i = 0; i < 2000; i += 1) {
        const value = rng.nextInt(bound);
        expect(value).toBeGreaterThanOrEqual(0);
        expect(value).toBeLessThan(bound);
      }
    }
  });
});

describe("Rng distribution", () => {
  it("has a mean near 0.5 over 100k floats", () => {
    const rng = new Rng(2024);
    let total = 0;
    for (let i = 0; i < 100_000; i += 1) total += rng.nextFloat();
    const mean = total / 100_000;
    expect(mean).toBeGreaterThan(0.49);
    expect(mean).toBeLessThan(0.51);
  });

  it("spreads nextInt evenly across buckets", () => {
    // Rejection sampling should leave no modulo bias, including for a bound
    // that does not divide 2^32.
    const rng = new Rng(2025);
    const bound = 7;
    const draws = 70_000;
    const counts = new Array<number>(bound).fill(0);
    for (let i = 0; i < draws; i += 1) {
      const bucket = rng.nextInt(bound);
      counts[bucket] = (counts[bucket] ?? 0) + 1;
    }

    const expected = draws / bound;
    for (const count of counts) {
      expect(count).toBeGreaterThan(expected * 0.95);
      expect(count).toBeLessThan(expected * 1.05);
    }
  });
});

describe("Rng performance", () => {
  it("generates at least 10M nextU32 per second", () => {
    // Informational: the generator sits in the inner loop of every benchmark,
    // so a collapse here would quietly slow the whole registry down. The
    // assertion is deliberately far below the real number to survive a noisy
    // machine; the logged figure is the one worth watching.
    const rng = new Rng(1);
    const iterations = 5_000_000;

    const start = performance.now();
    let sink = 0;
    for (let i = 0; i < iterations; i += 1) sink = (sink ^ rng.nextU32()) >>> 0;
    const elapsedMs = performance.now() - start;

    const opsPerSec = iterations / (elapsedMs / 1000);
    console.log(
      `rng: ${(opsPerSec / 1e6).toFixed(1)}M nextU32/sec (${elapsedMs.toFixed(0)}ms for ${iterations.toLocaleString()})`,
    );

    expect(sink).toBeGreaterThanOrEqual(0);
    expect(opsPerSec).toBeGreaterThan(10_000_000);
  });
});
