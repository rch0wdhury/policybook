/**
 * Constant backoff — wait the same amount every time.
 *
 * The baseline, and the policy you get by accident when nobody thought about
 * it. `sleep(100)` in a loop is this.
 *
 * It has one real virtue: it is the fastest to recover from a *short* outage,
 * because it never stops checking frequently. If the service is back after 300
 * milliseconds, a client waiting 100 ms between attempts finds out almost
 * immediately, while an exponential backoff may already be sleeping for
 * seconds.
 *
 * And one serious vice, which is why nothing here recommends it: **the load it
 * puts on a struggling service does not fall as the outage continues**. Every
 * client keeps knocking at a fixed rate, so a service that is down because it
 * is overloaded stays overloaded. Worse, clients that failed together stay in
 * step: with no randomness anywhere, every retry from every client lands in the
 * same instant, forever. That is the thundering herd in its purest form.
 *
 * Use it when the number of clients is small and known, and the outages you
 * expect are short. Otherwise use
 * [exponential-full-jitter](../exponential-full-jitter/).
 */

import type { RetryError } from "../../../packages/core/src/domains/retry/interface.ts";

export interface ConstantParams {
  /** The delay before every retry, in milliseconds. */
  baseMs: number;
  /** Give up after this many attempts. */
  maxAttempts: number;
}

const DEFAULT_BASE_MS = 100;
const DEFAULT_MAX_ATTEMPTS = 8;

export default class Constant {
  private readonly baseMs: number;
  private readonly maxAttempts: number;

  constructor(params: Partial<ConstantParams> = {}) {
    const baseMs = params.baseMs ?? DEFAULT_BASE_MS;
    const maxAttempts = params.maxAttempts ?? DEFAULT_MAX_ATTEMPTS;

    if (!Number.isInteger(baseMs) || baseMs < 0) {
      throw new RangeError(
        `Constant: baseMs must be a non-negative integer, received ${baseMs}`,
      );
    }
    if (!Number.isInteger(maxAttempts) || maxAttempts < 1) {
      throw new RangeError(
        `Constant: maxAttempts must be a positive integer, received ${maxAttempts}`,
      );
    }

    this.baseMs = baseMs;
    this.maxAttempts = maxAttempts;
  }

  nextDelay(attempt: number, error: RetryError): number | null {
    // This policy draws nothing at all, which is exactly what is wrong with it:
    // every client that failed together comes back together, forever.

    // Nothing is gained by retrying a failure the server says is permanent.
    if (!error.retryable) return null;
    if (attempt >= this.maxAttempts) return null;

    return this.baseMs;
  }
}
