/**
 * What a cache benchmark reports.
 *
 * Hit rate is the number that matters; evictions are reported because two
 * policies can reach the same hit rate while doing very different amounts of
 * work, and eviction count is the cheapest visible proxy for that.
 *
 * Throughput is deliberately absent here: it is machine-dependent, so it lives
 * in the `perf` section of `bench.json` and is never asserted.
 */

import { round6 } from "../../metrics";
import type { CacheHarnessResult } from "./harness";

export interface CacheMetrics {
  /** Hits divided by events, rounded to six places. */
  hitRate: number;
  /** Entries removed to make room. */
  evictions: number;
}

export function cacheMetrics(result: CacheHarnessResult): CacheMetrics {
  return {
    hitRate: result.events === 0 ? 0 : round6(result.hits / result.events),
    evictions: result.evictions,
  };
}
