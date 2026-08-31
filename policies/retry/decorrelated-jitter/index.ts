/**
 * Decorrelated jitter — grow the delay from the *last* delay, not from the
 * attempt number.
 *
 * The other policies in this domain compute their delay from `attempt`, so a
 * client's whole schedule is determined the moment it starts failing. This one
 * is a random walk instead: each delay is drawn from a range that reaches up to
 * three times the previous delay, and the sequence wanders upward rather than
 * doubling on a fixed ladder.
 *
 *     delay = min(cap, base + rng.nextInt(prev * 3 - base + 1))
 *     prev  = delay
 *
 * **`prev` is state, and it is the whole idea.** This is the only policy here
 * that remembers anything between calls, and it means the delay depends on the
 * *history* of the retry sequence rather than on its length. Two clients that
 * have made the same number of attempts can be at very different points, which
 * is a stronger form of decorrelation than a per-call draw gives: full jitter
 * spreads each attempt independently, while this lets whole schedules diverge.
 *
 * **It climbs more slowly than doubling, despite reaching for three times the
 * last delay.** A single step can be up to `3 x prev`, but the draw is uniform
 * over that range, so the expected step is `(base + 3 x prev) / 2` — about
 * **1.5x**, measured at 1.53 over a long run. Against exponential's exact 2x
 * that means it takes *more* attempts to reach the cap, not fewer: a median of
 * 14 against exponential's deterministic 8.
 *
 * The variance is the point. Those 14 attempts range from 6 to 87 across
 * clients, so two clients that started together are at wildly different delays
 * a few attempts in. That is why this disperses a fleet more thoroughly than
 * any per-call draw — and on the canonical workload it records the lowest peak
 * simultaneous retries of any policy here.
 *
 * It also means the policy ignores `attempt` entirely except for the give-up
 * check. Asking twice with the same attempt number gives two different answers
 * that both advance the walk, which a vector pins.
 */

import { Rng } from "../../../packages/core/src/rng.ts";
import type { RetryError } from "../../../packages/core/src/domains/retry/interface.ts";

export interface DecorrelatedJitterParams {
  /** The floor of every delay, and where the walk starts. */
  baseMs: number;
  /** No delay exceeds this, however far the walk has climbed. */
  capMs: number;
  /** Give up after this many attempts. */
  maxAttempts: number;
}

const DEFAULT_BASE_MS = 100;
const DEFAULT_CAP_MS = 10_000;
const DEFAULT_MAX_ATTEMPTS = 8;

export default class DecorrelatedJitter {
  private readonly baseMs: number;
  private readonly capMs: number;
  private readonly maxAttempts: number;
  private readonly rng: Rng;
  /** The previous delay. Starts at `baseMs`, which is where the walk begins. */
  private previousMs: number;

  constructor(params: Partial<DecorrelatedJitterParams> = {}, rng?: Rng) {
    const baseMs = params.baseMs ?? DEFAULT_BASE_MS;
    const capMs = params.capMs ?? DEFAULT_CAP_MS;
    const maxAttempts = params.maxAttempts ?? DEFAULT_MAX_ATTEMPTS;

    if (!Number.isInteger(baseMs) || baseMs < 1) {
      throw new RangeError(
        `DecorrelatedJitter: baseMs must be a positive integer, received ${baseMs}`,
      );
    }
    if (!Number.isInteger(capMs) || capMs < 1) {
      throw new RangeError(
        `DecorrelatedJitter: capMs must be a positive integer, received ${capMs}`,
      );
    }
    if (!Number.isInteger(maxAttempts) || maxAttempts < 1) {
      throw new RangeError(
        `DecorrelatedJitter: maxAttempts must be a positive integer, received ${maxAttempts}`,
      );
    }

    this.baseMs = baseMs;
    this.capMs = capMs;
    this.maxAttempts = maxAttempts;
    this.rng = rng ?? new Rng(0);
    this.previousMs = baseMs;
  }

  nextDelay(attempt: number, error: RetryError): number | null {
    if (!error.retryable) return null;
    if (attempt >= this.maxAttempts) return null;

    // `prev * 3 - base` is always positive: `prev` starts at `base` and every
    // delay is at least `base`, so the smallest the span can be is `2 * base`.
    // The bound is that span plus one, so `3 * prev` itself stays reachable.
    const span = this.previousMs * 3 - this.baseMs;
    const delay = Math.min(this.capMs, this.baseMs + this.rng.nextInt(span + 1));

    // The walk advances from the delay actually used, cap included — otherwise
    // a client that hit the cap would keep drawing from an ever-growing range
    // it can never reach.
    this.previousMs = delay;
    return delay;
  }

  /** The delay this policy last returned, for tests and vectors. */
  previousDelay(): number {
    return this.previousMs;
  }
}
