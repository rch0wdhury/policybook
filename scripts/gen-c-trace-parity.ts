/**
 * Generates the C trace parity tests from the committed reference prefixes.
 *
 * The Python parity test loads `trace-prefix.json` at runtime. C will not gain
 * a JSON parser just for a test, so the reference is compiled in. This is what makes the C benchmark numbers comparable with the
 * canonical TypeScript ones: if the C generator consumed a different number of
 * random draws, or summed its Zipf weights in a different order, the sequences
 * would split and this test would say exactly where.
 *
 * One test per domain, because domains do not share an event shape: a cache
 * event is a key, and a rate-limiter event is a time and a key. Each domain
 * registers an emitter below.
 *
 * Usage: pnpm tsx scripts/gen-c-trace-parity.ts
 */

import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname } from "node:path";
import { fileURLToPath } from "node:url";

/** A cache prefix: one key array per trace. */
interface CachePrefixFile {
  domain: string;
  events: number;
  traces: Record<string, number[]>;
}

/** A rate-limiter prefix: arrival times and keys per trace. */
interface RateLimiterPrefixFile {
  domain: string;
  events: number;
  traces: Record<string, { times: number[]; keys: number[] }>;
}

/**
 * A kv-cache prefix: float32 bit patterns of the first steps, plus a hash.
 *
 * The only domain whose trace is floating-point, and so the only one compared
 * on bit patterns rather than values.
 */
interface KvCachePrefixFile {
  domain: string;
  events: number;
  traces: Record<string, { steps: number; firstStepBits: number[][]; hash: number }>;
}

/** A C identifier derived from a trace id: `zipf-1.0-100k` → `zipf_1_0_100k`. */
function identifier(traceId: string): string {
  return traceId.replace(/[^A-Za-z0-9]/g, "_");
}

/** Wraps a list of numbers into indented C initialiser lines. */
function initialiser(values: number[], perLine: number): string[] {
  const lines: string[] = [];
  for (let index = 0; index < values.length; index += perLine) {
    lines.push(`    ${values.slice(index, index + perLine).join("u, ")}u,`);
  }
  return lines;
}

function header(domain: string, events: number, includes: string[]): string[] {
  return [
    "/*",
    " * GENERATED FILE — do not edit.",
    " *",
    " * Produced by scripts/gen-c-trace-parity.ts from",
    ` * packages/core/src/domains/${domain}/trace-prefix.json. Regenerate with:`,
    " *",
    " *     pnpm tsx scripts/gen-c-trace-parity.ts",
    " *",
    ` * The first ${events.toLocaleString()} events of every canonical ${domain} trace, as`,
    " * produced by the reference TypeScript generator. The C generator has to",
    " * reproduce them exactly, which is what makes C benchmark numbers comparable",
    " * with the canonical ones.",
    " */",
    "",
    "#include <stddef.h>",
    "#include <stdint.h>",
    "",
    ...includes,
    "",
    '#include "../pb_test.h"',
    "",
  ];
}

function readPrefix<T>(domain: string): T {
  const source = new URL(
    `../packages/core/src/domains/${domain}/trace-prefix.json`,
    import.meta.url,
  );
  return JSON.parse(readFileSync(source, "utf8")) as T;
}

function write(name: string, lines: string[]): void {
  const target = new URL(`../packages/c/tests/gen/${name}`, import.meta.url);
  mkdirSync(dirname(fileURLToPath(target)), { recursive: true });
  writeFileSync(target, lines.join("\n"));
}

/** The cache domain: a flat array of keys per trace. */
function emitCache(): number {
  const reference = readPrefix<CachePrefixFile>("cache");
  const traceIds = Object.keys(reference.traces);

  const out: string[] = header(reference.domain, reference.events, [
    '#include "policybook/allocator.h"',
    '#include "policybook/cache/traces.h"',
  ]);

  for (const traceId of traceIds) {
    const values = reference.traces[traceId] ?? [];
    out.push(`static const uint32_t expected_${identifier(traceId)}[] = {`);
    out.push(...initialiser(values, 12));
    out.push("};");
    out.push("");
  }

  out.push(
    "/*",
    " * Compare a generated trace against the reference.",
    " *",
    " * Only the first divergence is reported: once two pseudo-random streams",
    " * split, every later event differs too, and ten thousand failures would",
    " * bury the one that matters.",
    " */",
    "static void check_trace(const char *id, const uint32_t *expected, size_t count)",
    "{",
    "    const pb_cache_trace_spec *spec = pb_cache_trace_find(id);",
    "    uint32_t *actual;",
    "    size_t produced;",
    "    size_t i;",
    "    size_t mismatches = 0;",
    "",
    "    PB_CHECK(spec != NULL);",
    "    if (spec == NULL) {",
    "        return;",
    "    }",
    "",
    "    actual = (uint32_t *)pb_alloc(NULL, count * sizeof(uint32_t));",
    "    PB_CHECK(actual != NULL);",
    "    if (actual == NULL) {",
    "        return;",
    "    }",
    "",
    "    produced = pb_cache_trace_generate(spec, actual, count, NULL);",
    "    PB_CHECK(produced == count);",
    "",
    "    for (i = 0; i < produced && i < count; ++i) {",
    "        if (actual[i] != expected[i]) {",
    "            if (mismatches == 0) {",
    "                fprintf(stderr,",
    '                        "FAIL %s diverges from the reference at event %lu: "',
    '                        "got %lu, expected %lu\\n",',
    "                        id, (unsigned long)i, (unsigned long)actual[i],",
    "                        (unsigned long)expected[i]);",
    "            }",
    "            mismatches += 1;",
    "        }",
    "    }",
    "",
    "    PB_CHECK(mismatches == 0);",
    "    pb_free(NULL, actual, count * sizeof(uint32_t));",
    "}",
    "",
    "int main(void)",
    "{",
  );

  for (const traceId of traceIds) {
    const name = `expected_${identifier(traceId)}`;
    out.push(
      `    check_trace(${JSON.stringify(traceId)}, ${name}, sizeof(${name}) / sizeof(${name}[0]));`,
    );
  }
  out.push('    return pb_test_summary("test_trace_parity");', "}", "");

  write("test_trace_parity.c", out);
  return traceIds.reduce((sum, id) => sum + (reference.traces[id]?.length ?? 0), 0);
}

/** The rate-limiter domain: parallel time and key arrays per trace. */
function emitRateLimiter(): number {
  const reference = readPrefix<RateLimiterPrefixFile>("rate-limiter");
  const traceIds = Object.keys(reference.traces);

  const out: string[] = header(reference.domain, reference.events, [
    '#include "policybook/allocator.h"',
    '#include "policybook/rate_limiter/traces.h"',
  ]);

  for (const traceId of traceIds) {
    const entry = reference.traces[traceId];
    const id = identifier(traceId);
    out.push(`static const uint32_t expected_${id}_times[] = {`);
    out.push(...initialiser(entry?.times ?? [], 12));
    out.push("};", "");
    out.push(`static const uint32_t expected_${id}_keys[] = {`);
    out.push(...initialiser(entry?.keys ?? [], 12));
    out.push("};", "");
  }

  out.push(
    "/*",
    " * Compare a generated trace against the reference.",
    " *",
    " * Both the arrival times and the keys have to match: a port could get the",
    " * Bernoulli process right and still sample keys from a differently ordered",
    " * Zipf table. Only the first divergence of each is reported — once two",
    " * pseudo-random streams split, every later event differs too.",
    " *",
    " * The expected count is passed rather than derived from the buffers because",
    " * an arrival is the outcome of a Bernoulli trial, not a step of the loop:",
    " * two of these traces are shorter than the 10,000-event prefix length.",
    " */",
    "static void check_trace(const char *id, const uint32_t *expected_times,",
    "                        const uint32_t *expected_keys, size_t count)",
    "{",
    "    const pb_ratelimiter_trace_spec *spec = pb_ratelimiter_trace_find(id);",
    "    uint32_t *times;",
    "    uint32_t *keys;",
    "    size_t produced;",
    "    size_t i;",
    "    size_t time_mismatches = 0;",
    "    size_t key_mismatches = 0;",
    "",
    "    PB_CHECK(spec != NULL);",
    "    if (spec == NULL) {",
    "        return;",
    "    }",
    "",
    "    times = (uint32_t *)pb_alloc(NULL, count * sizeof(uint32_t));",
    "    keys = (uint32_t *)pb_alloc(NULL, count * sizeof(uint32_t));",
    "    PB_CHECK(times != NULL && keys != NULL);",
    "    if (times == NULL || keys == NULL) {",
    "        pb_free(NULL, times, count * sizeof(uint32_t));",
    "        pb_free(NULL, keys, count * sizeof(uint32_t));",
    "        return;",
    "    }",
    "",
    "    produced = pb_ratelimiter_trace_generate(spec, times, keys, count, NULL);",
    "    PB_CHECK(produced == count);",
    "",
    "    for (i = 0; i < produced && i < count; ++i) {",
    "        if (times[i] != expected_times[i]) {",
    "            if (time_mismatches == 0) {",
    "                fprintf(stderr,",
    '                        "FAIL %s arrival times diverge at event %lu: "',
    '                        "got %lu ms, expected %lu ms\\n",',
    "                        id, (unsigned long)i, (unsigned long)times[i],",
    "                        (unsigned long)expected_times[i]);",
    "            }",
    "            time_mismatches += 1;",
    "        }",
    "        if (keys[i] != expected_keys[i]) {",
    "            if (key_mismatches == 0) {",
    "                fprintf(stderr,",
    '                        "FAIL %s keys diverge at event %lu: "',
    '                        "got %lu, expected %lu\\n",',
    "                        id, (unsigned long)i, (unsigned long)keys[i],",
    "                        (unsigned long)expected_keys[i]);",
    "            }",
    "            key_mismatches += 1;",
    "        }",
    "    }",
    "",
    "    PB_CHECK(time_mismatches == 0);",
    "    PB_CHECK(key_mismatches == 0);",
    "    pb_free(NULL, times, count * sizeof(uint32_t));",
    "    pb_free(NULL, keys, count * sizeof(uint32_t));",
    "}",
    "",
    "int main(void)",
    "{",
  );

  for (const traceId of traceIds) {
    const id = identifier(traceId);
    out.push(
      `    check_trace(${JSON.stringify(traceId)}, expected_${id}_times, expected_${id}_keys,`,
      `                sizeof(expected_${id}_times) / sizeof(expected_${id}_times[0]));`,
    );
  }
  out.push('    return pb_test_summary("test_ratelimiter_trace_parity");', "}", "");

  write("test_ratelimiter_trace_parity.c", out);
  return traceIds.reduce(
    (sum, id) => sum + (reference.traces[id]?.times.length ?? 0),
    0,
  );
}

/**
 * The retry domain: one outage duration per episode.
 *
 * The same flat-array shape the cache uses, because a retry episode's identity
 * is a single draw. The generated test is otherwise the cache's, with the
 * domain's own spec lookup and generator.
 */
function emitRetry(): number {
  const reference = readPrefix<CachePrefixFile>("retry");
  const traceIds = Object.keys(reference.traces);

  const out: string[] = header(reference.domain, reference.events, [
    '#include "policybook/allocator.h"',
    '#include "policybook/retry/traces.h"',
  ]);

  for (const traceId of traceIds) {
    const values = reference.traces[traceId] ?? [];
    out.push(`static const uint32_t expected_${identifier(traceId)}[] = {`);
    out.push(...initialiser(values, 12));
    out.push("};");
    out.push("");
  }

  out.push(
    "/*",
    " * Compare the generated outages against the reference.",
    " *",
    " * The outage is the first draw of each episode's environment stream, so a",
    " * port whose Rng or per-episode seeding differed would diverge here before",
    " * anywhere else. Only the first divergence is reported.",
    " */",
    "static void check_trace(const char *id, const uint32_t *expected, size_t count)",
    "{",
    "    const pb_retry_trace_spec *spec = pb_retry_trace_find(id);",
    "    uint32_t *actual;",
    "    size_t produced;",
    "    size_t i;",
    "    size_t mismatches = 0;",
    "",
    "    PB_CHECK(spec != NULL);",
    "    if (spec == NULL) {",
    "        return;",
    "    }",
    "",
    "    actual = (uint32_t *)pb_alloc(NULL, count * sizeof(uint32_t));",
    "    PB_CHECK(actual != NULL);",
    "    if (actual == NULL) {",
    "        return;",
    "    }",
    "",
    "    produced = pb_retry_trace_generate(spec, actual, count, NULL);",
    "    PB_CHECK(produced == count);",
    "",
    "    for (i = 0; i < produced && i < count; ++i) {",
    "        if (actual[i] != expected[i]) {",
    "            if (mismatches == 0) {",
    "                fprintf(stderr,",
    '                        "FAIL %s diverges from the reference at episode %lu: "',
    '                        "got an outage of %lu ms, expected %lu ms\\n",',
    "                        id, (unsigned long)i, (unsigned long)actual[i],",
    "                        (unsigned long)expected[i]);",
    "            }",
    "            mismatches += 1;",
    "        }",
    "    }",
    "",
    "    PB_CHECK(mismatches == 0);",
    "    pb_free(NULL, actual, count * sizeof(uint32_t));",
    "}",
    "",
    "int main(void)",
    "{",
  );

  for (const traceId of traceIds) {
    const name = `expected_${identifier(traceId)}`;
    out.push(
      `    check_trace(${JSON.stringify(traceId)}, ${name}, sizeof(${name}) / sizeof(${name}[0]));`,
    );
  }
  out.push('    return pb_test_summary("test_retry_trace_parity");', "}", "");

  write("test_retry_trace_parity.c", out);
  return traceIds.reduce((sum, id) => sum + (reference.traces[id]?.length ?? 0), 0);
}

/**
 * The kv-cache domain: float32 bit patterns, and a hash of the whole trace.
 *
 * The only domain compared on bits rather than values. Two floats that print
 * alike can differ in the last place, and a single differing ULP propagates
 * through the normalisation into every other weight of that step — so for a
 * generator whose whole job is reproducibility, that difference *is* the bug.
 *
 * The steps have different lengths (step t holds t weights), so the expected
 * bits are emitted as one flat array plus offsets rather than a ragged 2-D
 * initialiser, which C would need padding for.
 */
function emitKvCache(): number {
  const reference = readPrefix<KvCachePrefixFile>("kv-cache");
  const traceIds = Object.keys(reference.traces);

  const out: string[] = header(reference.domain, reference.events, [
    // memcpy, for reading a float's bits without an aliasing violation.
    "#include <string.h>",
    "",
    '#include "policybook/allocator.h"',
    '#include "policybook/kv_cache/traces.h"',
  ]);

  let totalWeights = 0;
  for (const traceId of traceIds) {
    const entry = reference.traces[traceId];
    const id = identifier(traceId);
    const steps = entry?.firstStepBits ?? [];
    const flat = steps.flat();
    totalWeights += flat.length;

    out.push(`/* The first ${steps.length} steps of ${traceId}, as float32 bit patterns. */`);
    out.push(`static const uint32_t expected_${id}_bits[] = {`);
    out.push(...initialiser(flat, 8));
    out.push("};", "");
    out.push(`#define EXPECTED_${id.toUpperCase()}_STEPS ${steps.length}`);
    out.push("");
    out.push(`static const uint32_t expected_${id}_hash = ${entry?.hash ?? 0}u;`);
    out.push("");
  }

  out.push(
    "/*",
    " * Compare a generated trace against the reference, bit for bit.",
    " *",
    " * Step t holds t weights, so the expected bits are one flat array walked",
    " * with a running offset. Only the first divergence is reported: once two",
    " * streams split, every later weight differs too.",
    " *",
    " * The hash covers every step rather than the committed first few, so a port",
    " * that matched for ten steps and drifted at the first heavy-hitter redraw",
    " * still fails — which is the case the first steps alone could not catch.",
    " */",
    "static void check_trace(const char *id, const uint32_t *expected_bits, size_t steps,",
    "                        uint32_t expected_hash)",
    "{",
    "    const pb_kvcache_trace_spec *spec = pb_kvcache_trace_find(id);",
    "    pb_kvcache_trace_gen gen;",
    "    size_t offset = 0;",
    "    size_t step;",
    "    size_t mismatches = 0;",
    "    uint32_t hash;",
    "",
    "    PB_CHECK(spec != NULL);",
    "    if (spec == NULL) {",
    "        return;",
    "    }",
    "",
    "    PB_CHECK(pb_kvcache_trace_gen_init(&gen, spec, NULL) == 0);",
    "",
    "    for (step = 1; step <= steps; ++step) {",
    "        size_t len = 0;",
    "        size_t i;",
    "        const float *weights = pb_kvcache_trace_gen_next(&gen, &len);",
    "",
    "        PB_CHECK(weights != NULL);",
    "        PB_CHECK(len == step);",
    "        if (weights == NULL || len != step) {",
    "            break;",
    "        }",
    "",
    "        for (i = 0; i < len; ++i) {",
    "            uint32_t bits;",
    "            memcpy(&bits, &weights[i], sizeof(bits));",
    "            if (bits != expected_bits[offset + i]) {",
    "                if (mismatches == 0) {",
    "                    fprintf(stderr,",
    '                            "FAIL %s diverges from the reference at step %lu, "',
    '                            "position %lu: got bits 0x%08lx, expected 0x%08lx\\n",',
    "                            id, (unsigned long)step, (unsigned long)i,",
    "                            (unsigned long)bits,",
    "                            (unsigned long)expected_bits[offset + i]);",
    "                }",
    "                mismatches += 1;",
    "            }",
    "        }",
    "        offset += len;",
    "    }",
    "",
    "    PB_CHECK(mismatches == 0);",
    "    pb_kvcache_trace_gen_destroy(&gen);",
    "",
    "    hash = pb_kvcache_trace_hash(spec, NULL);",
    "    if (hash != expected_hash) {",
    "        fprintf(stderr,",
    '                "FAIL %s hashes to 0x%08lx over the whole trace, expected 0x%08lx\\n",',
    "                id, (unsigned long)hash, (unsigned long)expected_hash);",
    "    }",
    "    PB_CHECK(hash == expected_hash);",
    "}",
    "",
    "int main(void)",
    "{",
  );

  for (const traceId of traceIds) {
    const id = identifier(traceId);
    out.push(
      `    check_trace(${JSON.stringify(traceId)}, expected_${id}_bits,`,
      `                EXPECTED_${id.toUpperCase()}_STEPS, expected_${id}_hash);`,
    );
  }
  out.push('    return pb_test_summary("test_kvcache_trace_parity");', "}", "");

  write("test_kvcache_trace_parity.c", out);
  return totalWeights;
}

function main(): void {
  const cacheEvents = emitCache();
  console.log(
    `wrote packages/c/tests/gen/test_trace_parity.c — cache, ${cacheEvents.toLocaleString()} events`,
  );

  const rateLimiterEvents = emitRateLimiter();
  console.log(
    "wrote packages/c/tests/gen/test_ratelimiter_trace_parity.c — rate-limiter, " +
      `${rateLimiterEvents.toLocaleString()} events`,
  );

  const retryEpisodes = emitRetry();
  console.log(
    `wrote packages/c/tests/gen/test_retry_trace_parity.c — retry, ${retryEpisodes.toLocaleString()} episodes`,
  );

  const kvCacheWeights = emitKvCache();
  console.log(
    "wrote packages/c/tests/gen/test_kvcache_trace_parity.c — kv-cache, " +
      `${kvCacheWeights.toLocaleString()} weights + a hash of every step`,
  );
}

main();
