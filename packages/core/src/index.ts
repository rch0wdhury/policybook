/**
 * @policybook/core — shared runtime for every Policybook domain.
 *
 * This package holds the pieces every policy and harness depends on: the
 * deterministic random number generator, the canonical trace generators, the
 * per-domain interfaces and simulators, and the metric definitions. It contains
 * no Node-only APIs, so the same code runs in CI, in the CLI, and in the
 * browser.
 *
 * Contents arrive with their milestones: Rng (T02), the cache domain (T07),
 * rate-limiter and retry (T22, T26), kv-cache (T29).
 */

export { Rng, mix32 } from "./rng";
export { stableJson } from "./json";
export { round6 } from "./metrics";
export { ZipfSampler, zipfWeight } from "./zipf";
export type { ZipfAlpha } from "./zipf";

/** Version of the core runtime, recorded in every generated `bench.json`. */
export const CORE_VERSION = "0.1.0";
