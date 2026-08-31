/**
 * Generates `packages/c/tests/gen/test_rng.c` from the shared Rng vectors.
 *
 * C cannot reflect on method names or parse JSON without a dependency, so where
 * TypeScript and Python each have a generic runner, C gets a generated test
 * program (concept.md §12.2). The output is committed, so the C tree builds and
 * tests on its own with nothing but a compiler.
 *
 * Usage: pnpm tsx scripts/gen-c-rng-vectors.ts
 */

import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname } from "node:path";
import { fileURLToPath } from "node:url";

interface RngVectors {
  generator: string;
  version: number;
  seeds: {
    seed: number;
    nextU32: number[];
    nextFloat: string[];
    nextInt: { bound: number; values: number[] }[];
  }[];
  mix32: { input: number; output: number }[];
}

/** Formats a list of numbers as a wrapped C initialiser body. */
function formatArray(values: (number | string)[], indent: string, perLine: number): string {
  const lines: string[] = [];
  for (let index = 0; index < values.length; index += perLine) {
    lines.push(indent + values.slice(index, index + perLine).join(", ") + ",");
  }
  return lines.join("\n");
}

function u32Literal(value: number): string {
  return `${value >>> 0}u`;
}

function main(): void {
  const source = new URL("../packages/core/src/rng.vectors.json", import.meta.url);
  const vectors = JSON.parse(readFileSync(source, "utf8")) as RngVectors;

  const out: string[] = [];
  out.push("/*");
  out.push(" * GENERATED FILE — do not edit.");
  out.push(" *");
  out.push(" * Produced by scripts/gen-c-rng-vectors.ts from");
  out.push(" * packages/core/src/rng.vectors.json. Regenerate with:");
  out.push(" *");
  out.push(" *     pnpm tsx scripts/gen-c-rng-vectors.ts");
  out.push(" *");
  out.push(` * Generator under test: ${vectors.generator}`);
  out.push(" *");
  out.push(" * This is the proof that the C generator agrees with the TypeScript and");
  out.push(" * Python ones, bit for bit, which is what makes every seeded policy and");
  out.push(" * trace reproducible across languages (concept.md §9).");
  out.push(" */");
  out.push("");
  out.push("#include <stddef.h>");
  out.push("#include <stdint.h>");
  out.push("");
  out.push('#include "policybook/rng.h"');
  out.push("");
  out.push('#include "../pb_test.h"');
  out.push("");

  // Each sequence starts from a freshly seeded generator, matching the note in
  // the vectors file itself.
  for (const entry of vectors.seeds) {
    const seed = u32Literal(entry.seed);

    out.push(`static void test_seed_${entry.seed >>> 0}(void)`);
    out.push("{");
    out.push("    pb_rng rng;");
    out.push("    size_t i;");
    out.push("");

    out.push(`    static const uint32_t expected_u32[] = {`);
    out.push(formatArray(entry.nextU32.map(u32Literal), "        ", 6));
    out.push("    };");
    out.push(`    pb_rng_init(&rng, ${seed});`);
    out.push("    for (i = 0; i < sizeof(expected_u32) / sizeof(expected_u32[0]); ++i) {");
    out.push("        PB_CHECK_U32(pb_rng_next_u32(&rng), expected_u32[i]);");
    out.push("    }");
    out.push("");

    // The decimals below are the shortest round-tripping representation of each
    // double. next_float is u32 / 2^32, an exact power-of-two scaling, so the
    // comparison is exact rather than approximate.
    out.push(`    static const double expected_float[] = {`);
    out.push(formatArray(entry.nextFloat, "        ", 4));
    out.push("    };");
    out.push(`    pb_rng_init(&rng, ${seed});`);
    out.push("    for (i = 0; i < sizeof(expected_float) / sizeof(expected_float[0]); ++i) {");
    out.push("        PB_CHECK_DOUBLE_EXACT(pb_rng_next_float(&rng), expected_float[i]);");
    out.push("    }");
    out.push("");

    for (const testCase of entry.nextInt) {
      const bound = u32Literal(testCase.bound);
      out.push(`    static const uint32_t expected_int_${testCase.bound}[] = {`);
      out.push(formatArray(testCase.values.map(u32Literal), "        ", 8));
      out.push("    };");
      out.push(`    pb_rng_init(&rng, ${seed});`);
      out.push(
        `    for (i = 0; i < sizeof(expected_int_${testCase.bound}) / ` +
          `sizeof(expected_int_${testCase.bound}[0]); ++i) {`,
      );
      out.push(
        `        PB_CHECK_U32(pb_rng_next_int(&rng, ${bound}), expected_int_${testCase.bound}[i]);`,
      );
      out.push("    }");
      out.push("");
    }

    out.push("}");
    out.push("");
  }

  out.push("static void test_mix32(void)");
  out.push("{");
  for (const pair of vectors.mix32) {
    out.push(`    PB_CHECK_U32(pb_mix32(${u32Literal(pair.input)}), ${u32Literal(pair.output)});`);
  }
  out.push("}");
  out.push("");

  out.push("int main(void)");
  out.push("{");
  for (const entry of vectors.seeds) {
    out.push(`    test_seed_${entry.seed >>> 0}();`);
  }
  out.push("    test_mix32();");
  out.push('    return pb_test_summary("test_rng");');
  out.push("}");
  out.push("");

  const target = new URL("../packages/c/tests/gen/test_rng.c", import.meta.url);
  mkdirSync(dirname(fileURLToPath(target)), { recursive: true });
  writeFileSync(target, out.join("\n"));

  const checks =
    vectors.seeds.reduce(
      (total, entry) =>
        total +
        entry.nextU32.length +
        entry.nextFloat.length +
        entry.nextInt.reduce((sum, testCase) => sum + testCase.values.length, 0),
      0,
    ) + vectors.mix32.length;
  console.log(`wrote packages/c/tests/gen/test_rng.c — ${checks} checks`);
}

main();
