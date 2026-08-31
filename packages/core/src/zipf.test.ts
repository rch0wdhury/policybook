import { describe, expect, it } from "vitest";
import { Rng } from "./rng";
import { ZipfSampler, zipfWeight } from "./zipf";

describe("zipfWeight", () => {
  it("matches 1/rank for alpha 1", () => {
    expect(zipfWeight(0, 1)).toBe(1);
    expect(zipfWeight(1, 1)).toBe(0.5);
    expect(zipfWeight(9, 1)).toBe(0.1);
  });

  it("matches r^-0.75 without calling pow", () => {
    // The sqrt formulation has to agree with pow to within rounding — that is
    // what makes it a legitimate substitution rather than a different
    // distribution. It is used *instead of* pow because pow is not correctly
    // rounded across C standard libraries.
    for (const rank of [0, 1, 2, 3, 9, 99, 9_999]) {
      const viaSqrt = zipfWeight(rank, 0.75);
      const viaPow = Math.pow(rank + 1, -0.75);
      expect(Math.abs(viaSqrt - viaPow)).toBeLessThan(1e-15);
    }
  });

  it("decreases with rank", () => {
    for (const alpha of [1, 0.75] as const) {
      let previous = Infinity;
      for (let rank = 0; rank < 100; rank += 1) {
        const weight = zipfWeight(rank, alpha);
        expect(weight).toBeLessThan(previous);
        previous = weight;
      }
    }
  });
});

describe("ZipfSampler", () => {
  it("rejects a bad size", () => {
    expect(() => new ZipfSampler(0, 1)).toThrow(RangeError);
    expect(() => new ZipfSampler(2.5, 1)).toThrow(RangeError);
  });

  it("only ever returns ranks in range", () => {
    const sampler = new ZipfSampler(10, 1);
    const rng = new Rng(1);
    for (let draw = 0; draw < 10_000; draw += 1) {
      const rank = sampler.sample(rng);
      expect(rank).toBeGreaterThanOrEqual(0);
      expect(rank).toBeLessThan(10);
    }
  });

  it("consumes exactly one random draw per sample", () => {
    // Trace parity across languages depends on this: if a port consumed two
    // draws, every subsequent event would diverge.
    const sampler = new ZipfSampler(1_000, 1);
    const sampled = new Rng(7);
    sampler.sample(sampled);

    const direct = new Rng(7);
    direct.nextFloat();

    expect(sampled.nextU32()).toBe(direct.nextU32());
  });

  it("produces frequencies proportional to the weights (alpha 1)", () => {
    const size = 50;
    const sampler = new ZipfSampler(size, 1);
    const rng = new Rng(2024);
    const counts = new Array<number>(size).fill(0);
    const draws = 200_000;

    for (let draw = 0; draw < draws; draw += 1) {
      const rank = sampler.sample(rng);
      counts[rank] = (counts[rank] ?? 0) + 1;
    }

    // Rank 0 should be about twice rank 1, and about ten times rank 9.
    expect(counts[0]! / counts[1]!).toBeGreaterThan(1.85);
    expect(counts[0]! / counts[1]!).toBeLessThan(2.15);
    expect(counts[0]! / counts[9]!).toBeGreaterThan(9);
    expect(counts[0]! / counts[9]!).toBeLessThan(11);
  });

  it("is flatter at alpha 0.75 than at alpha 1", () => {
    const size = 50;
    const draws = 200_000;

    const frequencyRatio = (alpha: 1 | 0.75): number => {
      const sampler = new ZipfSampler(size, alpha);
      const rng = new Rng(99);
      let first = 0;
      let second = 0;
      for (let draw = 0; draw < draws; draw += 1) {
        const rank = sampler.sample(rng);
        if (rank === 0) first += 1;
        if (rank === 1) second += 1;
      }
      return first / second;
    };

    // 2^0.75 is about 1.68, against 2.0 for alpha 1.
    const flat = frequencyRatio(0.75);
    expect(flat).toBeGreaterThan(1.55);
    expect(flat).toBeLessThan(1.82);
    expect(flat).toBeLessThan(frequencyRatio(1));
  });

  it("is deterministic for a given seed", () => {
    const sampler = new ZipfSampler(1_000, 1);
    const first = Array.from({ length: 500 }, (): number => 0);
    const rngA = new Rng(5);
    for (let index = 0; index < first.length; index += 1) first[index] = sampler.sample(rngA);

    const rngB = new Rng(5);
    const second = first.map(() => sampler.sample(rngB));
    expect(second).toEqual(first);
  });
});
