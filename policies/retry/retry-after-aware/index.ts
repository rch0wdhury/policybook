/**
 * Retry-After-aware — do what the server asked, and guess only when it didn't.
 *
 * Every other policy in this domain is guessing. This one reads the answer when
 * the server has provided it: a `Retry-After` header is the service's own
 * statement of when it expects to be ready, and no amount of backoff theory
 * beats being told. When the header is absent it falls back to
 * [full jitter](../exponential-full-jitter/), so it is never worse than the
 * default.
 *
 *     if error carries a Retry-After:  delay = min(cap, retryAfterMs)
 *     otherwise:                       delay = rng.nextInt(ceiling + 1)
 *
 * **The clamp is not a formality.** A server under real load can ask for
 * minutes, and a client that honours an arbitrary hint has handed a stranger
 * control of its own latency budget. `capMs` is the caller's statement of how
 * long it is willing to be told to wait; past that, the request is treated as
 * advice rather than instruction.
 *
 * **This policy re-synchronises clients, and that is the honest cost.** A
 * thousand clients told "come back in five seconds" all come back in five
 * seconds — precisely the herd that jitter exists to break. Whether the trade
 * is worth it depends on what the server does with the herd it asked for: a
 * service that returns `Retry-After` from a token bucket has already reserved
 * capacity for that instant and will cope; one that returns a fixed constant
 * under load has not, and will be hit again just as hard. Use this against
 * services whose `Retry-After` you believe, and prefer
 * [full jitter](../exponential-full-jitter/) against ones you do not.
 *
 * The benchmark understates the risk — see the README.
 */

import { Rng } from "../../../packages/core/src/rng.ts";
import type { RetryError } from "../../../packages/core/src/domains/retry/interface.ts";

/**
 * `min(cap, base * 2^(attempt - 1))`, in integers and without overflowing.
 *
 * Restated rather than imported from a sibling policy, because `policybook add`
 * copies a policy file whole. `backoff-policies.test.ts` pins the copies
 * against each other.
 */
function backoffCeiling(attempt: number, baseMs: number, capMs: number): number {
  let delay = baseMs;
  for (let step = 1; step < attempt; step += 1) {
    if (delay >= capMs) return capMs;
    delay *= 2;
  }
  return Math.min(delay, capMs);
}

export interface RetryAfterAwareParams {
  /** The first fallback ceiling, doubled on each subsequent attempt. */
  baseMs: number;
  /** The longest wait this client will accept, from any source. */
  capMs: number;
  /** Give up after this many attempts. */
  maxAttempts: number;
}

const DEFAULT_BASE_MS = 100;
const DEFAULT_CAP_MS = 10_000;
const DEFAULT_MAX_ATTEMPTS = 8;

export default class RetryAfterAware {
  private readonly baseMs: number;
  private readonly capMs: number;
  private readonly maxAttempts: number;
  private readonly rng: Rng;

  constructor(params: Partial<RetryAfterAwareParams> = {}, rng?: Rng) {
    const baseMs = params.baseMs ?? DEFAULT_BASE_MS;
    const capMs = params.capMs ?? DEFAULT_CAP_MS;
    const maxAttempts = params.maxAttempts ?? DEFAULT_MAX_ATTEMPTS;

    if (!Number.isInteger(baseMs) || baseMs < 1) {
      throw new RangeError(
        `RetryAfterAware: baseMs must be a positive integer, received ${baseMs}`,
      );
    }
    if (!Number.isInteger(capMs) || capMs < 1) {
      throw new RangeError(
        `RetryAfterAware: capMs must be a positive integer, received ${capMs}`,
      );
    }
    if (!Number.isInteger(maxAttempts) || maxAttempts < 1) {
      throw new RangeError(
        `RetryAfterAware: maxAttempts must be a positive integer, received ${maxAttempts}`,
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

    const hint = error.retryAfterMs;
    if (hint !== undefined) {
      // A hint of zero is a real instruction — "come back now" — and is
      // honoured. Absent and zero are different statements, which is why the
      // check is for `undefined` rather than for falsiness.
      if (!Number.isInteger(hint) || hint < 0) {
        throw new RangeError(
          `RetryAfterAware: retryAfterMs must be a non-negative integer, received ${hint}`,
        );
      }
      // No draw is consumed on this path. A port that drew anyway would leave
      // its stream in a different place and diverge on the next fallback.
      return Math.min(this.capMs, hint);
    }

    return this.rng.nextInt(backoffCeiling(attempt, this.baseMs, this.capMs) + 1);
  }
}
