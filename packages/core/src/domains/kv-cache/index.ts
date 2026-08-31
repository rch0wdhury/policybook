/**
 * The `kv-cache` domain: which tokens to forget during LLM decoding.
 *
 * See `interface.ts` for the contract every policy implements, `TRACES.md` for
 * the synthetic attention workload and the caveats that come with it, and
 * `policies/kv-cache/` for the policies themselves.
 */

export type { KvCacheParams, KvCachePolicy } from "./interface";
export { KV_CACHE_BUDGETS } from "./interface";
export {
  KV_CACHE_TRACES,
  float32Bits,
  generateKvCacheTrace,
  hashKvCacheTrace,
} from "./traces";
export type { KvCacheTraceSpec } from "./traces";
export { runKvCacheTrace } from "./harness";
export type { KvCacheHarnessOptions, KvCacheHarnessResult } from "./harness";
export { kvCacheMetrics } from "./metrics";
export type { KvCacheMetrics } from "./metrics";
