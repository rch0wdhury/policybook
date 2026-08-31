/**
 * The retry simulator.
 *
 * Replays a set of independent episodes against a policy. An episode is one
 * outage: the service is down for `d` milliseconds, a client attempts at t=0,
 * and the policy decides how long to wait before each further attempt. The
 * episode ends when an attempt succeeds, the policy gives up, or the deadline
 * passes.
 *
 * As with the other harnesses this is the smallest thing that can produce the
 * numbers, it allocates nothing per attempt, and it polices the contract: a
 * policy returning a negative or non-integer delay fails here rather than
 * quietly producing a wrong mean.
 */

import { Rng } from "../../rng";
import type { RetryError, RetryPolicy } from "./interface";
import { environmentSeed, policySeed } from "./traces";
import type { RetryTraceSpec } from "./traces";

export interface RetryHarnessResult {
  episodes: number;
  successes: number;
  /** Attempts made, summed over every episode including the failures. */
  totalAttempts: number;
  /** Time to success, summed over successful episodes only. */
  totalTimeToSuccessMs: number;
  /** Attempts per episode, ascending. Kept for the percentile. */
  attemptsSorted: Uint32Array;
  /** Episodes abandoned because the policy returned null. */
  gaveUp: number;
  /** Episodes abandoned because the next attempt would fall past the deadline. */
  deadlineExceeded: number;
  /** Retries (every attempt after the first), summed over all episodes. */
  totalRetries: number;
  /**
   * The most retries landing in any single {@link HERD_WINDOW_MS} window.
   *
   * Every episode's first attempt is at t=0, so the thousand episodes are also
   * a thousand clients that failed simultaneously — which is the situation
   * jitter exists for. This counts how many of them come back at the same
   * moment.
   */
  peakRetriesInWindow: number;
}

/**
 * The window over which simultaneous retries are counted.
 *
 * A tenth of the reference base delay, and the size matters. It has to be small
 * relative to the first delay, or it cannot tell a policy that puts every
 * client at exactly t=100 from one that spreads them across [0, 100] — which is
 * the entire difference jitter makes. At a 100 ms window the two are
 * indistinguishable and full jitter scores *worse*, because its early draws
 * cluster near zero; at 10 ms it scores six times better, which is the truth.
 */
export const HERD_WINDOW_MS = 10;

/** The status a failing attempt reports. Nothing here branches on it. */
const SERVICE_UNAVAILABLE = 503;
/** The most a server ever asks a client to wait, in milliseconds. */
const MAX_RETRY_AFTER_MS = 5_000;

/** Builds a policy for one episode, given that episode's own random stream. */
export type RetryPolicyFactory = (rng: Rng) => RetryPolicy;

/**
 * Run a policy over every episode of `spec`.
 *
 * The policy is built **per episode** rather than once, because a jittered
 * policy holds its own random stream and every episode must get a fresh,
 * independently seeded one. A policy built once and reused would carry state
 * across episodes that are meant to be independent.
 *
 * Two random streams per episode. The **environment** stream draws the outage
 * and then one success roll per attempt; the **policy** stream is whatever the
 * policy itself draws. Keeping them apart is what makes the comparison fair:
 * every policy faces the identical outage and the identical sequence of coin
 * flips, so a difference in the results is a difference in strategy rather than
 * in luck.
 */
export function runRetryEpisodes(
  factory: RetryPolicyFactory,
  spec: RetryTraceSpec,
  maxEpisodes?: number,
): RetryHarnessResult {
  const episodes =
    maxEpisodes === undefined ? spec.episodes : Math.min(maxEpisodes, spec.episodes);
  const flakeThreshold = (100 - spec.flakePercent) / 100;

  const attemptsSorted = new Uint32Array(episodes);
  // One bucket per herd window across the whole deadline, allocated once.
  const herd = new Uint32Array(Math.floor(spec.deadlineMs / HERD_WINDOW_MS) + 1);
  let successes = 0;
  let totalAttempts = 0;
  let totalTimeToSuccessMs = 0;
  let gaveUp = 0;
  let deadlineExceeded = 0;
  let totalRetries = 0;

  // Reused across episodes so the loop allocates nothing.
  const error: RetryError = { status: SERVICE_UNAVAILABLE, retryable: true, retryAfterMs: 0 };

  for (let episode = 0; episode < episodes; episode += 1) {
    const environment = new Rng(environmentSeed(spec, episode));
    const policy = factory(new Rng(policySeed(spec, episode)));

    const outageMs = environment.nextInt(spec.maxOutageMs);
    let now = 0;
    let attempt = 1;

    for (;;) {
      // The roll is drawn on every attempt, whether or not the outage has
      // passed, so attempt `k` always consumes environment draw `k`. Drawing
      // only when it could matter would make the stream position depend on the
      // outage, and two policies making the same number of attempts would then
      // see different rolls.
      const roll = environment.nextFloat();
      if (now >= outageMs && roll < flakeThreshold) {
        successes += 1;
        totalTimeToSuccessMs += now;
        break;
      }

      error.retryAfterMs = Math.min(Math.max(outageMs - now, 0), MAX_RETRY_AFTER_MS);
      const delay = policy.nextDelay(attempt, error);

      if (delay === null) {
        gaveUp += 1;
        break;
      }
      if (!Number.isInteger(delay) || delay < 0) {
        throw new Error(
          `runRetryEpisodes: policy returned a delay of ${delay} on attempt ${attempt}. ` +
            "A delay must be a non-negative integer number of milliseconds, or null to give up.",
        );
      }

      now += delay;
      if (now > spec.deadlineMs) {
        deadlineExceeded += 1;
        break;
      }

      // This attempt is a retry, and `now` is when the service will feel it.
      // The bucket is always in range: `now` has just been checked against the
      // deadline, and `herd` is sized for the whole of it.
      totalRetries += 1;
      const bucket = Math.floor(now / HERD_WINDOW_MS);
      herd[bucket] = (herd[bucket] ?? 0) + 1;
      attempt += 1;
    }

    attemptsSorted[episode] = attempt;
    totalAttempts += attempt;
  }

  attemptsSorted.sort();

  let peakRetriesInWindow = 0;
  for (const count of herd) {
    if (count > peakRetriesInWindow) peakRetriesInWindow = count;
  }

  return {
    episodes,
    successes,
    totalAttempts,
    totalTimeToSuccessMs,
    attemptsSorted,
    gaveUp,
    deadlineExceeded,
    totalRetries,
    peakRetriesInWindow,
  };
}
