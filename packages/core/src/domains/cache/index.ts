/**
 * The `cache` domain: eviction for a fixed-capacity key cache.
 *
 * See `interface.ts` for the contract every policy implements, `TRACES.md` for
 * the canonical workloads, and `policies/cache/` for the policies themselves.
 */

export type { CacheMeta, CacheParams, CachePolicy } from "./interface";
export { CACHE_TRACES, SCAN_INTERVAL, SHIFT_INTERVAL, generateCacheTrace } from "./traces";
export type { CacheTraceSpec } from "./traces";
export { runCacheTrace } from "./harness";
export type { CacheHarnessOptions, CacheHarnessResult } from "./harness";
export { cacheMetrics } from "./metrics";
export type { CacheMetrics } from "./metrics";
