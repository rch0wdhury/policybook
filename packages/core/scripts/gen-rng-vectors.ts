/**
 * Regenerates `packages/core/src/rng.vectors.json` from the reference Rng.
 *
 * These vectors are what a Python or C port is checked against, so they are the
 * definition of "the same generator". Running this without changing `rng.ts`
 * must produce a zero diff.
 *
 * Usage: pnpm tsx packages/core/scripts/gen-rng-vectors.ts
 */

import { writeFileSync } from "node:fs";
import { stableJson } from "../src/json";
import { Rng, mix32 } from "../src/rng";

/** Seeds covered by the vectors: small, adjacent, arbitrary, and high-bit-set. */
const SEEDS = [1, 2, 42, 0xdeadbeef];

/** Bounds exercised by `nextInt`: power of two, prime, round, and large. */
const BOUNDS = [2, 7, 100, 1_000_000];

const U32_COUNT = 32;
const FLOAT_COUNT = 8;
const INT_COUNT = 16;

/** Inputs for the `mix32` key hash: edges, small values, and known constants. */
const MIX32_INPUTS = [
  0, 1, 2, 42, 123456789, 0x9e3779b9, 0xdeadbeef, 0xffffffff,
];

interface SeedVectors {
  seed: number;
  nextU32: number[];
  nextFloat: string[];
  nextInt: { bound: number; values: number[] }[];
}

function vectorsForSeed(seed: number): SeedVectors {
  // Every sequence starts from a freshly constructed Rng, so a port can replay
  // any one of them on its own without having to reproduce the others first.
  const u32 = new Rng(seed);
  const nextU32 = Array.from({ length: U32_COUNT }, () => u32.nextU32());

  const floats = new Rng(seed);
  const nextFloat = Array.from({ length: FLOAT_COUNT }, () =>
    // Shortest decimal string that round-trips to the same float64 — parsing it
    // in Python or with strtod recovers the identical double.
    String(floats.nextFloat()),
  );

  const nextInt = BOUNDS.map((bound) => {
    const rng = new Rng(seed);
    return {
      bound,
      values: Array.from({ length: INT_COUNT }, () => rng.nextInt(bound)),
    };
  });

  return { seed, nextU32, nextFloat, nextInt };
}

function main(): void {
  const file = {
    generator: "xoshiro128** seeded by splitmix32",
    version: 1,
    description:
      "Reference outputs for the Policybook Rng. Every sequence starts from a " +
      "freshly constructed Rng(seed). nextFloat values are decimal strings that " +
      "round-trip exactly to the intended float64. A port is conformant when it " +
      "reproduces all of these.",
    seeds: SEEDS.map(vectorsForSeed),
    mix32: MIX32_INPUTS.map((input) => ({ input, output: mix32(input) })),
  };

  const target = new URL("../src/rng.vectors.json", import.meta.url);
  writeFileSync(target, stableJson(file));
  console.log(
    `wrote ${SEEDS.length} seeds × (${U32_COUNT} u32, ${FLOAT_COUNT} float, ` +
      `${BOUNDS.length}×${INT_COUNT} int) + ${MIX32_INPUTS.length} mix32 pairs`,
  );
}

main();
