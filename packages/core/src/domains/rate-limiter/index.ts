/**
 * The `rate-limiter` domain: deciding whether a request may proceed.
 *
 * See `interface.ts` for the contract every policy implements, `TRACES.md` for
 * the canonical workloads, and `policies/rate-limiter/` for the policies
 * themselves.
 */

export type { RateLimiterPolicy } from "./interface";
export { RATE_LIMITER_REFERENCE } from "./interface";
export { RATE_LIMITER_TRACES, generateRateLimiterTrace } from "./traces";
export type { RateLimiterTrace, RateLimiterTraceSpec } from "./traces";
export { runRateLimiterTrace } from "./harness";
export type { RateLimiterHarnessOptions, RateLimiterHarnessResult } from "./harness";
export { rateLimiterMetrics } from "./metrics";
export type { RateLimiterMetrics } from "./metrics";
