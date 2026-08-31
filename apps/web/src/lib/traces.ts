/**
 * Which traces a runner offers, per domain.
 *
 * Listed rather than derived from the trace registry, so the order is
 * deliberate: the first is what a reader lands on, and it should be the trace
 * that shows the domain's problem most clearly rather than whichever happens to
 * sort first.
 *
 * `bursty` leads the rate limiters because it is where the policies visibly
 * disagree; `zipf-1.0-100k` leads the caches because it is the shape that makes
 * caching work at all.
 */
export const TRACES_BY_DOMAIN: Record<string, string[]> = {
  cache: ["zipf-1.0-100k", "scan-heavy", "shifting-popularity", "zipf-0.75-1m"],
  "rate-limiter": ["bursty", "steady", "overload", "many-keys"],
  "kv-cache": ["decode-4096"],
};
