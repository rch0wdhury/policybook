/**
 * Generates one C test program per policy from its `vectors.json`.
 *
 * TypeScript and Python each have a generic runner that reflects on method
 * names. C cannot reflect, and will not gain a JSON parser just for tests, so
 * the vectors are compiled into C ahead of time and committed — the C tree
 * builds and tests with nothing but a compiler (concept.md §12.2).
 *
 * How a call becomes C is a per-domain question: it depends on the domain's
 * vtable, its parameter struct and its return types. So this script owns
 * discovery, writing, staleness and the CMake manifest, and each domain
 * registers an emitter below. The cache emitter arrives with the cache domain
 * in T09.
 *
 * Usage: pnpm gen:c-vectors
 */

import { existsSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { discoverPolicies, findRepoRoot, loadVectors } from "../packages/vectors/src/discover";
import type { DiscoveredPolicy, JsonValue, VectorsFile } from "../packages/vectors/src/types";

/* -------------------------------------------------------------------------- */
/* Helpers available to every domain emitter                                   */
/* -------------------------------------------------------------------------- */

const FNV_OFFSET = 0xcbf29ce484222325n;
const FNV_PRIME = 0x100000001b3n;
const MASK_64 = 0xffffffffffffffffn;

/**
 * FNV-1a 64 of a string, matching `pb_fnv1a64_str` in the C tree.
 *
 * Vectors use string keys where the key is opaque; the C API takes uint64_t and
 * leaves hashing to the caller, so the generator maps them here and the C test
 * compares against the mapped value. BigInt is fine in a build script — the ban
 * on it in concept.md §9 is about policy arithmetic, which must stay
 * reproducible across languages.
 */
export function fnv1a64(text: string): bigint {
  let hash = FNV_OFFSET;
  for (const byte of new TextEncoder().encode(text)) {
    hash = (hash ^ BigInt(byte)) & MASK_64;
    hash = (hash * FNV_PRIME) & MASK_64;
  }
  return hash;
}

/** Renders a JSON key (string or number) as a `uint64_t` C literal. */
export function cKey(value: JsonValue): string {
  if (typeof value === "string") return `${fnv1a64(value).toString()}ULL`;
  if (typeof value === "number" && Number.isInteger(value) && value >= 0) {
    return `${value}ULL`;
  }
  throw new Error(`cannot express ${JSON.stringify(value)} as a uint64_t key`);
}

/** Renders a number as a C double literal that round-trips exactly. */
export function cDouble(value: number): string {
  if (!Number.isFinite(value)) {
    throw new Error(`cannot express ${value} as a C double literal`);
  }
  const text = String(value);
  return text.includes(".") || text.includes("e") ? text : `${text}.0`;
}

/** The banner every generated file carries. */
export function generatedHeader(policyId: string): string[] {
  return [
    "/*",
    " * GENERATED FILE — do not edit.",
    " *",
    ` * Vector test for ${policyId}, produced by scripts/gen-c-vectors.ts from`,
    ` * policies/${policyId}/vectors.json. Regenerate with:`,
    " *",
    " *     pnpm gen:c-vectors",
    " *",
    " * The C implementation is conformant when it reproduces these results, which",
    " * are the same ones the TypeScript and Python implementations are held to.",
    " */",
    "",
  ];
}

/* -------------------------------------------------------------------------- */
/* Domain emitters                                                             */
/* -------------------------------------------------------------------------- */

/** Produces the complete C source of a policy's vector test. */
export type CDomainEmitter = (policy: DiscoveredPolicy, vectors: VectorsFile) => string;

/** A C identifier fragment: `w-tinylfu` → `w_tinylfu`. */
function identifier(name: string): string {
  return name.replace(/-/g, "_");
}

/**
 * A C struct field name from a reference parameter name.
 *
 * Vectors use the reference (camelCase) spelling, so `windowFraction` becomes
 * `window_fraction`. Without this every multi-word parameter would have to be
 * spelled in camelCase throughout the C headers.
 */
function fieldName(name: string): string {
  return identifier(name).replace(/([a-z0-9])([A-Z])/g, "$1_$2").toLowerCase();
}

/**
 * Methods that only report state and change nothing.
 *
 * They exist for the TypeScript and Python tests and are not on the C vtable,
 * so their steps are dropped. Skipping is only safe because they are pure: a
 * method that is merely *unknown* is an error, not a skip, so a genuinely new
 * interface method can never be silently ignored.
 */
const CACHE_INTROSPECTION = new Set([
  "size",
  "frequencyOf",
  "isReferenced",
  "isVisited",
  "queueOf",
  "targetT1",
  "listOfKey",
  "segmentOf",
]);

/** Renders a vector argument as a C unsigned literal. */
function cUint(value: JsonValue, context: string): string {
  if (typeof value !== "number" || !Number.isInteger(value) || value < 0) {
    throw new Error(`${context}: expected a non-negative integer, got ${JSON.stringify(value)}`);
  }
  return String(value);
}

/** Renders a vector argument as a C boolean. */
function cBool(value: JsonValue, context: string): string {
  if (typeof value !== "boolean") {
    throw new Error(`${context}: expected a boolean, got ${JSON.stringify(value)}`);
  }
  return value ? "true" : "false";
}

/**
 * The `cache` domain emitter.
 *
 * Vector keys are strings for policies whose keys are opaque; the C API takes
 * uint64_t and leaves hashing to the caller, so they are mapped here with
 * FNV-1a 64 — the same function `pb_fnv1a64_str` implements, so a reader can
 * check any expectation by hand.
 */
const emitCache: CDomainEmitter = (policy, vectors) => {
  const base = identifier(policy.name);
  const vtable = `pb_cache_${base}`;
  const paramsType = `pb_cache_${base}_params`;
  const paramsDefault = `PB_CACHE_${base.toUpperCase()}_PARAMS_DEFAULT`;

  const out: string[] = [
    ...generatedHeader(policy.id),
    "#include <stdbool.h>",
    "#include <stddef.h>",
    "#include <stdint.h>",
    "",
    '#include "policybook/cache/cache.h"',
    `#include "policybook/cache/${base}.h"`,
    '#include "policybook/hash.h"',
    '#include "policybook/rng.h"',
    "",
    '#include "../pb_test.h"',
    "",
  ];

  vectors.cases.forEach((testCase, caseIndex) => {
    out.push(`/* ${testCase.name} */`);
    out.push(`static void case_${caseIndex}(void)`);
    out.push("{");
    out.push("    pb_rng rng;");
    out.push(`    ${paramsType} params = ${paramsDefault};`);
    out.push("    pb_cache *cache;");
    out.push("");
    out.push(`    pb_rng_init(&rng, ${(testCase.seed ?? 0) >>> 0}u);`);

    for (const [name, value] of Object.entries(testCase.params ?? {})) {
      if (typeof value !== "number" || !Number.isFinite(value)) {
        throw new Error(
          `${policy.id} case "${testCase.name}": parameter ${name} is ` +
            `${JSON.stringify(value)}, which the C emitter cannot express`,
        );
      }
      // Counts are uint32 fields, fractions are double fields. The literal's
      // form follows the value's, and the struct field decides the rest.
      const literal =
        Number.isInteger(value) && value >= 0 ? `${value}u` : cDouble(value);
      out.push(`    params.${fieldName(name)} = ${literal};`);
    }

    out.push(`    cache = ${vtable}.create(&params, NULL, &rng);`);
    out.push("    PB_CHECK(cache != NULL);");
    out.push("    if (cache == NULL) {");
    out.push("        return;");
    out.push("    }");
    out.push("");

    testCase.steps.forEach((step, stepIndex) => {
      const where = `${policy.id} case "${testCase.name}" step ${stepIndex}`;
      const args = step.args ?? [];
      const hasExpectation = Object.prototype.hasOwnProperty.call(step, "expect");

      switch (step.call) {
        case "onAccess": {
          if (args.length < 2) throw new Error(`${where}: onAccess needs a key and a hit flag`);
          const key = cKey(args[0] as JsonValue);
          const hit = cBool(args[1] as JsonValue, where);
          out.push(`    ${vtable}.on_access(cache, ${key}, ${hit}, NULL);`);
          break;
        }
        case "evict": {
          if (hasExpectation) {
            out.push(`    PB_CHECK_U64(${vtable}.evict(cache), ${cKey(step.expect as JsonValue)});`);
          } else {
            out.push(`    (void)${vtable}.evict(cache);`);
          }
          break;
        }
        case "admit": {
          if (args.length < 1) throw new Error(`${where}: admit needs a key`);
          const key = cKey(args[0] as JsonValue);
          if (hasExpectation) {
            out.push(
              `    PB_CHECK(${vtable}.admit(cache, ${key}, NULL) == ` +
                `${cBool(step.expect as JsonValue, where)});`,
            );
          } else {
            out.push(`    (void)${vtable}.admit(cache, ${key}, NULL);`);
          }
          break;
        }
        default: {
          if (!CACHE_INTROSPECTION.has(step.call)) {
            throw new Error(
              `${where}: no C mapping for "${step.call}". Add it to the cache emitter in ` +
                "scripts/gen-c-vectors.ts, or to CACHE_INTROSPECTION if it only reports state.",
            );
          }
          // Reporting-only: nothing to call, nothing to assert.
          out.push(`    /* ${step.call}() is not on the C vtable; step skipped */`);
        }
      }
    });

    out.push("");
    out.push(`    ${vtable}.destroy(cache);`);
    out.push("}");
    out.push("");
  });

  out.push("int main(void)");
  out.push("{");
  vectors.cases.forEach((_, caseIndex) => {
    out.push(`    case_${caseIndex}();`);
  });
  out.push(`    return pb_test_summary("${policy.id} vectors");`);
  out.push("}");
  out.push("");

  return out.join("\n");
};

/**
 * Methods the rate-limiter domain exposes for tests but not on the C vtable.
 *
 * Same rule as the cache's: skipping is safe only because these report state
 * and change nothing, and an *unknown* method is still a hard error.
 */
const RATE_LIMITER_INTROSPECTION = new Set([
  "countOf",
  "estimateOf",
  "levelOf",
  "requestsOf",
  "tokensOf",
]);

/**
 * The `rate-limiter` domain emitter.
 *
 * Keys are integers throughout this domain (§2.1), and time is an integer
 * number of milliseconds supplied by the caller, so every argument maps
 * directly onto the C signature with no hashing in between.
 */
const emitRateLimiter: CDomainEmitter = (policy, vectors) => {
  const base = identifier(policy.name);
  const vtable = `pb_ratelimiter_${base}`;
  const paramsType = `pb_ratelimiter_${base}_params`;
  const paramsDefault = `PB_RATELIMITER_${base.toUpperCase()}_PARAMS_DEFAULT`;

  const out: string[] = [
    ...generatedHeader(policy.id),
    "#include <stdbool.h>",
    "#include <stddef.h>",
    "#include <stdint.h>",
    "",
    '#include "policybook/rate_limiter/rate_limiter.h"',
    `#include "policybook/rate_limiter/${base}.h"`,
    '#include "policybook/rng.h"',
    "",
    '#include "../pb_test.h"',
    "",
  ];

  vectors.cases.forEach((testCase, caseIndex) => {
    out.push(`/* ${testCase.name} */`);
    out.push(`static void case_${caseIndex}(void)`);
    out.push("{");
    out.push("    pb_rng rng;");
    out.push(`    ${paramsType} params = ${paramsDefault};`);
    out.push("    pb_ratelimiter *limiter;");
    out.push("");
    out.push(`    pb_rng_init(&rng, ${(testCase.seed ?? 0) >>> 0}u);`);

    for (const [name, value] of Object.entries(testCase.params ?? {})) {
      if (typeof value !== "number" || !Number.isInteger(value) || value < 0) {
        throw new Error(
          `${policy.id} case "${testCase.name}": parameter ${name} is ` +
            `${JSON.stringify(value)}; rate-limiter parameters are non-negative integers`,
        );
      }
      out.push(`    params.${fieldName(name)} = ${value}u;`);
    }

    out.push(`    limiter = ${vtable}.create(&params, NULL, &rng);`);
    out.push("    PB_CHECK(limiter != NULL);");
    out.push("    if (limiter == NULL) {");
    out.push("        return;");
    out.push("    }");
    out.push("");

    testCase.steps.forEach((step, stepIndex) => {
      const where = `${policy.id} case "${testCase.name}" step ${stepIndex}`;
      const args = step.args ?? [];
      const hasExpectation = Object.prototype.hasOwnProperty.call(step, "expect");

      switch (step.call) {
        case "allow": {
          if (args.length < 3) {
            throw new Error(`${where}: allow needs a key, a cost and a time`);
          }
          const call = `${vtable}.allow(limiter, ${cKey(args[0] as JsonValue)}, ` +
            `${cUint(args[1] as JsonValue, where)}u, ${cKey(args[2] as JsonValue)})`;
          if (hasExpectation) {
            out.push(`    PB_CHECK(${call} == ${cBool(step.expect as JsonValue, where)});`);
          } else {
            out.push(`    (void)${call};`);
          }
          break;
        }
        case "retryAfter": {
          if (args.length < 2) throw new Error(`${where}: retryAfter needs a key and a time`);
          const call =
            `${vtable}.retry_after(limiter, ${cKey(args[0] as JsonValue)}, ` +
            `${cKey(args[1] as JsonValue)})`;
          if (hasExpectation) {
            out.push(`    PB_CHECK_U64(${call}, ${cKey(step.expect as JsonValue)});`);
          } else {
            out.push(`    (void)${call};`);
          }
          break;
        }
        case "stateSize": {
          const call = `${vtable}.state_size(limiter)`;
          if (hasExpectation) {
            out.push(
              `    PB_CHECK_U64((uint64_t)${call}, ${cKey(step.expect as JsonValue)});`,
            );
          } else {
            out.push(`    (void)${call};`);
          }
          break;
        }
        default: {
          if (!RATE_LIMITER_INTROSPECTION.has(step.call)) {
            throw new Error(
              `${where}: no C mapping for "${step.call}". Add it to the rate-limiter emitter ` +
                "in scripts/gen-c-vectors.ts, or to RATE_LIMITER_INTROSPECTION if it only " +
                "reports state.",
            );
          }
          out.push(`    /* ${step.call}() is not on the C vtable; step skipped */`);
        }
      }
    });

    out.push("");
    out.push(`    ${vtable}.destroy(limiter);`);
    out.push("}");
    out.push("");
  });

  out.push("int main(void)");
  out.push("{");
  vectors.cases.forEach((_, caseIndex) => {
    out.push(`    case_${caseIndex}();`);
  });
  out.push(`    return pb_test_summary("${policy.id} vectors");`);
  out.push("}");
  out.push("");

  return out.join("\n");
};

/**
 * Methods the retry domain exposes for tests but not on the C vtable.
 *
 * Same rule as the other domains': skipping is safe only because these report
 * state and change nothing, and an *unknown* method is still a hard error.
 */
const RETRY_INTROSPECTION = new Set(["previousDelay"]);

/**
 * The `retry` domain emitter.
 *
 * `nextDelay` takes an error object rather than scalars, so the emitter builds
 * a `pb_retry_error` literal from the vector's JSON. A null expectation becomes
 * `PB_RETRY_GIVE_UP`, which is why the C function returns a signed type.
 */
const emitRetry: CDomainEmitter = (policy, vectors) => {
  const base = identifier(policy.name);
  const vtable = `pb_retry_${base}`;
  const paramsType = `pb_retry_${base}_params`;
  const paramsDefault = `PB_RETRY_${base.toUpperCase()}_PARAMS_DEFAULT`;

  const out: string[] = [
    ...generatedHeader(policy.id),
    "#include <stdbool.h>",
    "#include <stddef.h>",
    "#include <stdint.h>",
    "",
    '#include "policybook/retry/retry.h"',
    `#include "policybook/retry/${base}.h"`,
    '#include "policybook/rng.h"',
    "",
    '#include "../pb_test.h"',
    "",
  ];

  /** A `pb_retry_error` initialiser from the vector's error object. */
  const cError = (value: JsonValue, where: string): string => {
    if (typeof value !== "object" || value === null || Array.isArray(value)) {
      throw new Error(`${where}: expected an error object, got ${JSON.stringify(value)}`);
    }
    const error = value as Record<string, JsonValue>;
    const status = error["status"];
    const retryable = error["retryable"];
    const retryAfter = error["retryAfterMs"];

    if (status !== undefined && typeof status !== "number") {
      throw new Error(`${where}: error.status must be a number`);
    }
    if (typeof retryable !== "boolean") {
      throw new Error(`${where}: error.retryable must be a boolean`);
    }
    if (retryAfter !== undefined && typeof retryAfter !== "number") {
      throw new Error(`${where}: error.retryAfterMs must be a number`);
    }

    const hasRetryAfter = retryAfter !== undefined;
    return (
      `{ ${status ?? 0}, ${retryable ? "true" : "false"}, ` +
      `${hasRetryAfter ? "true" : "false"}, ${hasRetryAfter ? `${retryAfter}u` : "0u"} }`
    );
  };

  vectors.cases.forEach((testCase, caseIndex) => {
    out.push(`/* ${testCase.name} */`);
    out.push(`static void case_${caseIndex}(void)`);
    out.push("{");
    out.push("    pb_rng rng;");
    out.push(`    ${paramsType} params = ${paramsDefault};`);
    out.push("    pb_retry *policy;");
    out.push("    pb_retry_error error;");
    out.push("");
    out.push(`    pb_rng_init(&rng, ${(testCase.seed ?? 0) >>> 0}u);`);

    for (const [name, value] of Object.entries(testCase.params ?? {})) {
      if (typeof value !== "number" || !Number.isInteger(value) || value < 0) {
        throw new Error(
          `${policy.id} case "${testCase.name}": parameter ${name} is ` +
            `${JSON.stringify(value)}; retry parameters are non-negative integers`,
        );
      }
      out.push(`    params.${fieldName(name)} = ${value}u;`);
    }

    out.push(`    policy = ${vtable}.create(&params, NULL, &rng);`);
    out.push("    PB_CHECK(policy != NULL);");
    out.push("    if (policy == NULL) {");
    out.push("        return;");
    out.push("    }");
    out.push("");

    testCase.steps.forEach((step, stepIndex) => {
      const where = `${policy.id} case "${testCase.name}" step ${stepIndex}`;
      const args = step.args ?? [];
      const hasExpectation = Object.prototype.hasOwnProperty.call(step, "expect");

      if (step.call !== "nextDelay") {
        if (RETRY_INTROSPECTION.has(step.call)) {
          // Reporting-only: nothing to call, nothing to assert.
          out.push(`    /* ${step.call}() is not on the C vtable; step skipped */`);
          return;
        }
        throw new Error(
          `${where}: no C mapping for "${step.call}". The retry vtable has only ` +
            "next_delay; add a mapping to the retry emitter in scripts/gen-c-vectors.ts, " +
            "or to RETRY_INTROSPECTION if it only reports state.",
        );
      }
      if (args.length < 2) throw new Error(`${where}: nextDelay needs an attempt and an error`);

      const attempt = args[0] as JsonValue;
      if (typeof attempt !== "number" || !Number.isInteger(attempt) || attempt < 0) {
        throw new Error(`${where}: attempt must be a non-negative integer`);
      }

      out.push(`    error = (pb_retry_error)${cError(args[1] as JsonValue, where)};`);
      const call = `${vtable}.next_delay(policy, ${attempt}u, &error)`;
      if (hasExpectation) {
        const expected = step.expect as JsonValue;
        const literal = expected === null ? "PB_RETRY_GIVE_UP" : `${expected as number}`;
        out.push(`    PB_CHECK_I64(${call}, ${literal});`);
      } else {
        out.push(`    (void)${call};`);
      }
    });

    out.push("");
    out.push(`    ${vtable}.destroy(policy);`);
    out.push("}");
    out.push("");
  });

  out.push("int main(void)");
  out.push("{");
  vectors.cases.forEach((_, caseIndex) => {
    out.push(`    case_${caseIndex}();`);
  });
  out.push(`    return pb_test_summary("${policy.id} vectors");`);
  out.push("}");
  out.push("");

  return out.join("\n");
};

/**
 * Methods the kv-cache domain exposes for tests but not on the C vtable.
 *
 * Same rule as the other domains': skipping is safe only because these report
 * state and change nothing, and an *unknown* method is still a hard error.
 */
const KV_CACHE_INTROSPECTION = new Set([
  "keptCount",
  "sinkCount",
  "scoreOf",
  "votesOf",
  "lastAttentionOf",
  "windowScoreOf",
  "effectiveBudget",
]);

/**
 * Renders a number as a C float literal that round-trips exactly.
 *
 * Attention weights cross the language boundary as float32, so a vector value
 * that is not exactly representable there would mean the C test asserted
 * against a different number from the TypeScript one. Rather than rounding
 * quietly, this refuses — a hand-authored vector should use values that survive
 * the trip, and 0.5, 0.25 and 0.125 always do.
 */
function cFloat(value: JsonValue, context: string): string {
  if (typeof value !== "number" || !Number.isFinite(value)) {
    throw new Error(`${context}: expected a finite number, got ${JSON.stringify(value)}`);
  }
  if (Math.fround(value) !== value) {
    throw new Error(
      `${context}: ${value} is not exactly representable as a float32, so C and ` +
        "TypeScript would assert against different numbers. Use a value that is — " +
        "halves, quarters and eighths always are.",
    );
  }
  const text = String(value);
  return `${text.includes(".") || text.includes("e") ? text : `${text}.0`}f`;
}

/**
 * The `kv-cache` domain emitter.
 *
 * Two calls cross to C. `onDecodeStep` takes an attention array that becomes a
 * `static const float[]` per step, or NULL where the vector passes null.
 * `evict` writes into a caller-supplied buffer and returns a count, so the
 * expectation — a list of positions — is checked length-first and then
 * elementwise, which is also the order that gives the most useful failure.
 */
const emitKvCache: CDomainEmitter = (policy, vectors) => {
  const base = identifier(policy.name);
  const vtable = `pb_kvcache_${base}`;
  const paramsType = `pb_kvcache_${base}_params`;
  const paramsDefault = `PB_KVCACHE_${base.toUpperCase()}_PARAMS_DEFAULT`;

  /** Widest eviction any case expects, so the buffer is sized once. */
  let victimCapacity = 1;
  for (const testCase of vectors.cases) {
    for (const step of testCase.steps) {
      if (step.call === "evict" && Array.isArray(step.expect)) {
        victimCapacity = Math.max(victimCapacity, step.expect.length);
      }
    }
  }

  const out: string[] = [
    ...generatedHeader(policy.id),
    "#include <stddef.h>",
    "#include <stdint.h>",
    "",
    '#include "policybook/kv_cache/kv_cache.h"',
    `#include "policybook/kv_cache/${base}.h"`,
    '#include "policybook/rng.h"',
    "",
    '#include "../pb_test.h"',
    "",
    `#define VICTIM_CAPACITY ${victimCapacity}`,
    "",
  ];

  vectors.cases.forEach((testCase, caseIndex) => {
    // Attention arrays are file-scope constants: one per step that supplies
    // them, named for the case and step so a failure is traceable.
    const attentionNames = new Map<number, string>();
    testCase.steps.forEach((step, stepIndex) => {
      if (step.call !== "onDecodeStep") return;
      const attention = (step.args ?? [])[1] as JsonValue;
      if (!Array.isArray(attention)) return;

      const name = `attn_${caseIndex}_${stepIndex}`;
      const where = `${policy.id} case "${testCase.name}" step ${stepIndex}`;
      const literals = attention.map((weight) => cFloat(weight as JsonValue, where));
      out.push(`static const float ${name}[] = { ${literals.join(", ")} };`);
      attentionNames.set(stepIndex, name);
    });
    if (attentionNames.size > 0) out.push("");

    out.push(`/* ${testCase.name} */`);
    out.push(`static void case_${caseIndex}(void)`);
    out.push("{");
    out.push("    pb_rng rng;");
    out.push(`    ${paramsType} params = ${paramsDefault};`);
    out.push("    pb_kvcache *policy;");
    out.push("    uint32_t victims[VICTIM_CAPACITY];");
    out.push("    size_t evicted;");
    out.push("");
    out.push(`    pb_rng_init(&rng, ${(testCase.seed ?? 0) >>> 0}u);`);

    for (const [name, value] of Object.entries(testCase.params ?? {})) {
      if (typeof value !== "number" || !Number.isInteger(value) || value < 0) {
        throw new Error(
          `${policy.id} case "${testCase.name}": parameter ${name} is ` +
            `${JSON.stringify(value)}; kv-cache parameters are non-negative integers`,
        );
      }
      out.push(`    params.${fieldName(name)} = ${value}u;`);
    }

    out.push(`    policy = ${vtable}.create(&params, NULL, &rng);`);
    out.push("    PB_CHECK(policy != NULL);");
    out.push("    if (policy == NULL) {");
    out.push("        return;");
    out.push("    }");
    out.push("");

    testCase.steps.forEach((step, stepIndex) => {
      const where = `${policy.id} case "${testCase.name}" step ${stepIndex}`;
      const args = step.args ?? [];

      if (step.call === "onDecodeStep") {
        const pos = cUint(args[0] as JsonValue, `${where}: pos`);
        const attentionName = attentionNames.get(stepIndex);
        if (attentionName !== undefined) {
          out.push(
            `    ${vtable}.on_decode_step(policy, ${pos}u, ${attentionName},`,
            `                             sizeof(${attentionName}) / sizeof(${attentionName}[0]));`,
          );
        } else {
          out.push(`    ${vtable}.on_decode_step(policy, ${pos}u, NULL, 0);`);
        }
        return;
      }

      if (step.call === "evict") {
        const budget = cUint(args[0] as JsonValue, `${where}: budget`);
        out.push(
          `    evicted = ${vtable}.evict(policy, ${budget}u, victims, VICTIM_CAPACITY);`,
        );

        if (!Object.prototype.hasOwnProperty.call(step, "expect")) return;
        const expected = step.expect;
        if (!Array.isArray(expected)) {
          throw new Error(`${where}: evict expects a list of positions`);
        }

        out.push(`    PB_CHECK_U64(evicted, ${expected.length});`);
        if (expected.length > 0) {
          // Guarded so a short return does not read past what was written —
          // the count check above has already reported that failure.
          out.push(`    if (evicted == ${expected.length}) {`);
          expected.forEach((position, index) => {
            const literal = cUint(position as JsonValue, `${where}: victim ${index}`);
            out.push(`        PB_CHECK_U64(victims[${index}], ${literal});`);
          });
          out.push("    }");
        }
        return;
      }

      if (KV_CACHE_INTROSPECTION.has(step.call)) {
        // Reporting-only: nothing to call, nothing to assert.
        out.push(`    /* ${step.call}() is not on the C vtable; step skipped */`);
        return;
      }

      throw new Error(
        `${where}: no C mapping for "${step.call}". The kv-cache vtable has ` +
          "on_decode_step and evict; add a mapping to the kv-cache emitter in " +
          "scripts/gen-c-vectors.ts, or to KV_CACHE_INTROSPECTION if it only reports state.",
      );
    });

    out.push("");
    out.push(`    ${vtable}.destroy(policy);`);
    out.push("}");
    out.push("");
  });

  out.push("int main(void)");
  out.push("{");
  vectors.cases.forEach((_, caseIndex) => {
    out.push(`    case_${caseIndex}();`);
  });
  out.push(`    return pb_test_summary("${policy.id} vectors");`);
  out.push("}");
  out.push("");

  return out.join("\n");
};

/**
 * One emitter per domain, added alongside the domain's C interface.
 */
const EMITTERS: Record<string, CDomainEmitter> = {
  cache: emitCache,
  "rate-limiter": emitRateLimiter,
  "kv-cache": emitKvCache,
  retry: emitRetry,
};

/* -------------------------------------------------------------------------- */

function generatedName(policy: DiscoveredPolicy): string {
  return `${policy.domain}_${policy.name}_vectors.c`.replace(/-/g, "_");
}

function main(): void {
  const repoRoot = findRepoRoot();
  const genDir = join(repoRoot, "packages", "c", "tests", "gen");
  mkdirSync(genDir, { recursive: true });

  const policies = discoverPolicies(repoRoot).filter((policy) =>
    policy.meta.ports?.includes("c"),
  );

  const written: string[] = [];
  const missingEmitters: string[] = [];

  for (const policy of policies) {
    const emitter = EMITTERS[policy.domain];
    if (emitter === undefined) {
      missingEmitters.push(policy.domain);
      continue;
    }

    const vectors = loadVectors(policy.vectorsPath);
    const source = emitter(policy, vectors);
    const filename = generatedName(policy);
    const target = join(genDir, filename);

    const existing = existsSync(target) ? readFileSync(target, "utf8") : null;
    if (existing !== source) writeFileSync(target, source);
    written.push(filename);
  }

  if (missingEmitters.length > 0) {
    const unique = [...new Set(missingEmitters)].sort();
    console.error(
      `no C emitter for domain(s): ${unique.join(", ")}.\n` +
        "Add one to EMITTERS in scripts/gen-c-vectors.ts — it needs the domain's vtable,\n" +
        "its params struct, and how each interface method maps to a C call.",
    );
    process.exit(1);
  }

  // Drop tests for policies that no longer exist, so the C tree cannot keep
  // compiling a policy the registry has dropped.
  for (const entry of readdirSync(genDir)) {
    if (!entry.endsWith("_vectors.c")) continue;
    if (!written.includes(entry)) {
      rmSync(join(genDir, entry));
      console.log(`  removed stale ${entry}`);
    }
  }

  // The CMake side of the same list.
  const manifest = [
    "# GENERATED by scripts/gen-c-vectors.ts — do not edit.",
    "#",
    "# One vector test per policy declaring a C port. Included by CMakeLists.txt,",
    "# which defines pb_add_test.",
    "",
    ...written
      .slice()
      .sort()
      .map((filename) => {
        const target = filename.replace(/\.c$/, "");
        return `pb_add_test(${target} tests/gen/${filename})`;
      }),
    "",
  ].join("\n");

  const manifestPath = join(genDir, "manifest.cmake");
  const existingManifest = existsSync(manifestPath)
    ? readFileSync(manifestPath, "utf8")
    : null;
  if (existingManifest !== manifest) writeFileSync(manifestPath, manifest);

  console.log(
    `gen:c-vectors — ${written.length} vector test(s) for ${policies.length} C polic${
      policies.length === 1 ? "y" : "ies"
    }.`,
  );
}

main();
