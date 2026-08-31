/**
 * Equal jitter — half the exponential delay fixed, half of it random.
 *
 * The middle ground between [exponential](../exponential/), which spreads
 * nothing, and [full jitter](../exponential-full-jitter/), which spreads
 * everything and halves the expected wait in the process. Take the exponential
 * ceiling, keep half of it as a floor, and draw the rest uniformly:
 *
 *     half  = min(cap, base * 2^(attempt-1)) / 2
 *     delay = half + rng.nextInt(half + 1)
 *
 * So a delay is always somewhere in `[half, 2 x half]`, and averages about
 * three quarters of the un-jittered ceiling — against full jitter's one half.
 *
 * **Choose it when you want the backoff to actually back off.** Full jitter can
 * return zero on any attempt, which is what makes its arrival pattern so
 * smooth, but it also means a client can retry immediately eight times running
 * and give a struggling service no respite at all. Equal jitter guarantees a
 * floor that grows with the attempt number, so the load really does fall away
 * as an outage continues, while still spreading clients over a range wide
 * enough to break the herd.
 *
 * AWS's 2015 measurements found full jitter slightly ahead of this on both
 * completion time and total work, which is why full jitter is the registry's
 * default. Equal jitter is the answer when a guaranteed minimum delay matters
 * more than that margin — a downstream that needs time to recover, a rate limit
 * you must not trip again immediately.
 */

import { Rng } from "../../../packages/core/src/rng.ts";
import type { RetryError } from "../../../packages/core/src/domains/retry/interface.ts";

/**
 * `min(cap, base * 2^(attempt - 1))`, in integers and without overflowing.
 *
 * Restated in each policy rather than imported from a sibling, because
 * `policybook add` copies a policy file whole and a copy that reached back into
 * the registry would not compile in the reader's project. The copies are pinned
 * against each other in `backoff-policies.test.ts`.
 */
function backoffCeiling(attempt: number, baseMs: number, capMs: number): number {
  let delay = baseMs;
  for (let step = 1; step < attempt; step += 1) {
    if (delay >= capMs) return capMs;
    delay *= 2;
  }
  return Math.min(delay, capMs);
}

export interface EqualJitterParams {
  /** The first ceiling, doubled on each subsequent attempt. */
  baseMs: number;
  /** No ceiling exceeds this, however many attempts have failed. */
  capMs: number;
  /** Give up after this many attempts. */
  maxAttempts: number;
}

const DEFAULT_BASE_MS = 100;
const DEFAULT_CAP_MS = 10_000;
const DEFAULT_MAX_ATTEMPTS = 8;

export default class EqualJitter {
  private readonly baseMs: number;
  private readonly capMs: number;
  private readonly maxAttempts: number;
  private readonly rng: Rng;

  constructor(params: Partial<EqualJitterParams> = {}, rng?: Rng) {
    const baseMs = params.baseMs ?? DEFAULT_BASE_MS;
    const capMs = params.capMs ?? DEFAULT_CAP_MS;
    const maxAttempts = params.maxAttempts ?? DEFAULT_MAX_ATTEMPTS;

    if (!Number.isInteger(baseMs) || baseMs < 1) {
      throw new RangeError(`EqualJitter: baseMs must be a positive integer, received ${baseMs}`);
    }
    if (!Number.isInteger(capMs) || capMs < 1) {
      throw new RangeError(`EqualJitter: capMs must be a positive integer, received ${capMs}`);
    }
    if (!Number.isInteger(maxAttempts) || maxAttempts < 1) {
      throw new RangeError(
        `EqualJitter: maxAttempts must be a positive integer, received ${maxAttempts}`,
      );
    }

    this.baseMs = baseMs;
    this.capMs = capMs;
    this.maxAttempts = maxAttempts;
    this.rng = rng ?? new Rng(0);
  }

  nextDelay(attempt: number, error: RetryError): number | null {
    if (!error.retryable) return null;
    if (attempt >= this.maxAttempts) return null;

    // Integer halving. At a ceiling of 1 the half is 0 and every delay is 0 —
    // degenerate, but that is what the formula says, and a base of 1 ms is not
    // a configuration anyone should be relying on.
    const half = Math.floor(backoffCeiling(attempt, this.baseMs, this.capMs) / 2);
    return half + this.rng.nextInt(half + 1);
  }
}
