/**
 * The `retry` domain: how long to wait before trying again.
 *
 * See `interface.ts` for the contract every policy implements, `TRACES.md` for
 * the canonical workload, and `policies/retry/` for the policies themselves.
 */

export type { RetryError, RetryParams, RetryPolicy } from "./interface";
export { RETRY_REFERENCE } from "./interface";
export { RETRY_TRACES, environmentSeed, generateRetryTrace, policySeed } from "./traces";
export type { RetryTraceSpec } from "./traces";
export { HERD_WINDOW_MS, runRetryEpisodes } from "./harness";
export type { RetryHarnessResult, RetryPolicyFactory } from "./harness";
export { retryMetrics } from "./metrics";
export type { RetryMetrics } from "./metrics";
