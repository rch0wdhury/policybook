/**
 * What a kv-cache benchmark reports.
 *
 * **Every number here is a proxy.** They measure how much of the model's
 * attention a policy managed to keep, which is not the same as how good the
 * model's output was. The two correlate — a policy that throws away the tokens
 * the model was looking at will produce worse text — but the correlation is not
 * a guarantee and this registry cannot measure the thing itself without running
 * a model. The domain README says so at length, and any decision taken on these
 * numbers alone should be re-checked against real generations.
 */

import { round6 } from "../../metrics";
import type { KvCacheHarnessResult } from "./harness";

export interface KvCacheMetrics {
  /**
   * Mean share of the model's attention that survived, over all steps.
   *
   * One means the policy never dropped a position the model was looking at.
   * The interesting range is narrow — even a plain sliding window keeps most of
   * the mass, because most attention is recent — so read the differences rather
   * than the absolute figures.
   */
  retainedAttentionMass: number;
  /**
   * Mean share of the 32 most-attended positions still held.
   *
   * The metric that separates the policies. Attention mass is dominated by the
   * recency window, which every policy keeps; the heavy hitters are scattered
   * and old, and finding them is the entire reason the attention-aware policies
   * exist.
   */
  heavyHitterRecall: number;
  /** Times the policy was asked to evict. */
  evictionCalls: number;
}

export function kvCacheMetrics(result: KvCacheHarnessResult): KvCacheMetrics {
  return {
    retainedAttentionMass:
      result.steps === 0 ? 0 : round6(result.totalRetainedMass / result.steps),
    heavyHitterRecall:
      result.totalHeavyPossible === 0
        ? 0
        : round6(result.totalHeavyHits / result.totalHeavyPossible),
    evictionCalls: result.evictionCalls,
  };
}
