/**
 * Exponential backoff — double the wait after every failure, up to a cap.
 *
 * The textbook answer, and a genuine improvement on
 * [constant](../constant/): load on a struggling service falls off
 * geometrically as an outage continues, so the service gets quieter exactly
 * when it most needs to. The cap stops the delay running away — without it the
 * eighth retry of a 100 ms base would be nearly thirteen seconds, and the
 * twentieth would be a fortnight.
 *
 * **It still synchronises clients, and that is the reason not to use it.** The
 * delay is a pure function of the attempt number, so every client that failed
 * at the same moment retries at the same moment, every time, forever. Backing
 * off exponentially converts a continuous herd into a periodic one; it does not
 * disperse it. AWS's published analysis of this — the source for the jittered
 * variants in this domain — measured the difference and found the synchronised
 * version markedly worse under contention despite the identical delay curve.
 *
 * Ship [exponential-full-jitter](../exponential-full-jitter/) instead unless
 * you specifically need a delay sequence that is reproducible without a random
 * source.
 *
 * **The doubling is an integer shift, capped before it can overflow.** The
 * attempt number is bounded by `maxAttempts` before the shift happens, so
 * `base << 30` is never reached however the policy is configured.
 */

import type { RetryError } from "../../../packages/core/src/domains/retry/interface.ts";

export interface ExponentialParams {
  /** The first delay, doubled on each subsequent attempt. */
  baseMs: number;
  /** No delay exceeds this, however many attempts have failed. */
  capMs: number;
  /** Give up after this many attempts. */
  maxAttempts: number;
}

const DEFAULT_BASE_MS = 100;
const DEFAULT_CAP_MS = 10_000;
const DEFAULT_MAX_ATTEMPTS = 8;

/**
 * `min(cap, base * 2^(attempt - 1))`, in integers and without overflowing.
 *
 * Shared with the jittered variants, which differ only in what they do with
 * this number. Doubling stops as soon as the cap is reached, so the shift can
 * never run past the width of the integer no matter how large `attempt` is.
 */
export function backoffCeiling(attempt: number, baseMs: number, capMs: number): number {
  let delay = baseMs;
  for (let step = 1; step < attempt; step += 1) {
    if (delay >= capMs) return capMs;
    delay *= 2;
  }
  return Math.min(delay, capMs);
}

export default class Exponential {
  private readonly baseMs: number;
  private readonly capMs: number;
  private readonly maxAttempts: number;

  constructor(params: Partial<ExponentialParams> = {}) {
    const baseMs = params.baseMs ?? DEFAULT_BASE_MS;
    const capMs = params.capMs ?? DEFAULT_CAP_MS;
    const maxAttempts = params.maxAttempts ?? DEFAULT_MAX_ATTEMPTS;

    if (!Number.isInteger(baseMs) || baseMs < 1) {
      throw new RangeError(
        `Exponential: baseMs must be a positive integer, received ${baseMs}`,
      );
    }
    if (!Number.isInteger(capMs) || capMs < 1) {
      throw new RangeError(`Exponential: capMs must be a positive integer, received ${capMs}`);
    }
    if (!Number.isInteger(maxAttempts) || maxAttempts < 1) {
      throw new RangeError(
        `Exponential: maxAttempts must be a positive integer, received ${maxAttempts}`,
      );
    }

    this.baseMs = baseMs;
    this.capMs = capMs;
    this.maxAttempts = maxAttempts;
  }

  nextDelay(attempt: number, error: RetryError): number | null {
    // No draw anywhere: the delay is a pure function of the attempt number,
    // which is precisely why every client retries in lockstep.

    if (!error.retryable) return null;
    if (attempt >= this.maxAttempts) return null;

    return backoffCeiling(attempt, this.baseMs, this.capMs);
  }
}
