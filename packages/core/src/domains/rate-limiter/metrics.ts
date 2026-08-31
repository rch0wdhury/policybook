/**
 * What a rate-limiter benchmark reports.
 *
 * Accept rate alone cannot rank these policies: a limiter that accepts
 * everything scores best on it and is not a limiter. The number is reported
 * beside the burst it permitted and the fairness it achieved, because the whole
 * question in this domain is what a policy gives up to hold a line.
 */

import { round6 } from "../../metrics";
import type { RateLimiterHarnessResult } from "./harness";

export interface RateLimiterMetrics {
  /** Accepted divided by events, rounded to six places. */
  acceptRate: number;
  /**
   * The most accepts allowed in any 100 ms window.
   *
   * A 100 permits/second policy that never exceeds 10 here is smoothing; one
   * that reaches 100 is letting a full second's budget through at once. Neither
   * is wrong — it is the trade the domain exists to make visible.
   */
  maxBurst100ms: number;
  /**
   * Jain's fairness index over per-key accept counts, or null on single-key
   * traces where fairness is not a meaningful question.
   *
   * One means every key that arrived was accepted the same number of times;
   * `1/n` means one key took everything. Note this measures accepts, not accept
   * *rates*: on a Zipf trace an unfair-looking score can simply reflect that
   * popular keys asked more often, which is why it is only reported for
   * `many-keys` and read alongside the trace description.
   */
  jainFairness: number | null;
  /** Largest number of keys the policy tracked at once, or null if unreported. */
  entriesTracked: number | null;
}

export function rateLimiterMetrics(result: RateLimiterHarnessResult): RateLimiterMetrics {
  return {
    acceptRate: result.events === 0 ? 0 : round6(result.accepted / result.events),
    maxBurst100ms: result.maxBurst100ms,
    jainFairness: jainFairness(result),
    entriesTracked: result.entriesTracked,
  };
}

/**
 * `(sum x)^2 / (n * sum x^2)`, over the keys that actually appeared.
 *
 * Both sums arrive as exact integers from the harness, so this is the single
 * floating-point operation in the whole metric — which is what keeps the three
 * languages agreeing on the sixth decimal place.
 *
 * Keys that never arrived are excluded: including them would report a policy as
 * unfair for declining to serve traffic nobody sent.
 */
function jainFairness(result: RateLimiterHarnessResult): number | null {
  if (result.keysSeen < 2) return null;
  if (result.acceptSumSquares === 0) return null;
  return round6(
    (result.acceptSum * result.acceptSum) / (result.keysSeen * result.acceptSumSquares),
  );
}
