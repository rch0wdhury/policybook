/**
 * What a retry benchmark reports.
 *
 * Success rate alone ranks nothing: a policy that retries forever reaches 100%
 * and is the reason the service stayed down. It is reported beside how long
 * success took and how many attempts it cost, because the whole question in
 * this domain is what a client spends — its own latency, and the server's
 * capacity — to get an answer.
 */

import { round6 } from "../../metrics";
import type { RetryHarnessResult } from "./harness";

export interface RetryMetrics {
  /** Episodes that ended in success, over all episodes. */
  successRate: number;
  /** Mean time from the first attempt to the successful one, in milliseconds. */
  meanTimeToSuccessMs: number;
  /** Mean attempts per episode, counting the ones that failed. */
  meanAttempts: number;
  /**
   * The 99th percentile of attempts per episode.
   *
   * The tail is the number that matters operationally: the mean tells you what
   * a typical client costs the service, and this tells you what the worst
   * hundredth of them cost it during an incident.
   */
  p99Attempts: number;
  /**
   * The largest share of all retries arriving inside one 10 ms window.
   *
   * The thundering-herd measure, and the one that shows why jitter exists.
   * Every episode's first attempt is at t=0, so the episodes are also a fleet
   * of clients that failed together; this is the size of the spike the
   * recovering service feels when they come back.
   *
   * A policy with no randomness puts every client's *n*-th retry at exactly the
   * same instant, so this stays high however gentle the delay curve looks on
   * paper. Lower is better, and it is the axis on which the jittered policies
   * beat the ones they otherwise resemble.
   */
  peakRetryShare: number;
}

export function retryMetrics(result: RetryHarnessResult): RetryMetrics {
  const { episodes, successes } = result;

  return {
    successRate: episodes === 0 ? 0 : round6(successes / episodes),
    meanTimeToSuccessMs:
      successes === 0 ? 0 : round6(result.totalTimeToSuccessMs / successes),
    meanAttempts: episodes === 0 ? 0 : round6(result.totalAttempts / episodes),
    p99Attempts: percentile(result.attemptsSorted, 99),
    peakRetryShare:
      result.totalRetries === 0
        ? 0
        : round6(result.peakRetriesInWindow / result.totalRetries),
  };
}

/**
 * Nearest-rank percentile: the smallest value at or above which `p` percent of
 * the data sits.
 *
 * Stated explicitly because percentile definitions differ and three languages
 * have to agree on the sixth decimal place. There is no interpolation — the
 * result is always an observed attempt count, which is the honest thing to
 * report for a discrete quantity.
 */
function percentile(sorted: Uint32Array, p: number): number {
  if (sorted.length === 0) return 0;
  const rank = Math.ceil((p / 100) * sorted.length);
  const index = Math.min(Math.max(rank, 1), sorted.length) - 1;
  return sorted[index]!;
}
