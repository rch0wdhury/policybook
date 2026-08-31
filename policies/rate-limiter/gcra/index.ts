/**
 * GCRA — the token bucket, kept as one number instead of three.
 *
 * The Generic Cell Rate Algorithm comes from ATM traffic policing, where the
 * hardware could not afford a balance and a timestamp and a carry per virtual
 * circuit. Instead it stores a single **theoretical arrival time**: the instant
 * at which the next request would be exactly on schedule. Everything else —
 * how many permits are banked, how much of the next one has accrued — is
 * implied by how far that instant sits from now.
 *
 * **It admits and refuses exactly what [token bucket](../token-bucket/) does**,
 * including the fractional carry, and `retryAfter` agrees to the millisecond.
 * The TAT is not an approximation of the balance; it is the same information
 * written differently. A vector mirrors the token bucket's own case value for
 * value, and `bucket-policies.test.ts` checks the agreement over a random
 * trace.
 *
 * So the reason to choose it is **state**: one integer per key rather than
 * three. At a million keys that is 8 MB against 24 MB, and it is why GCRA is
 * what you find inside Redis rate-limiter modules and network gear.
 *
 * **The arithmetic is exact integers, in units scaled by the rate.** Written
 * the textbook way, GCRA needs an emission interval `T = 1000 / ratePerSec`
 * milliseconds, which is not a whole number for most rates — 3 permits a second
 * gives 333.33. Multiplying through by `ratePerSec` clears it: one permit costs
 * exactly 1,000 scaled units, one millisecond is `ratePerSec` of them, and
 * nothing is ever rounded. That is also precisely why this agrees with the
 * token bucket's thousandths-of-a-token carry rather than merely resembling it.
 *
 * The TAT stays exact while `now * ratePerSec` is below 2^53, which at 100
 * permits a second is about 2,800 years of millisecond clock.
 */

export interface GcraParams {
  /** Permits per second, sustained. */
  ratePerSec: number;
  /** How many permits may be spent at once after an idle period. */
  burst: number;
}

const DEFAULT_RATE_PER_SEC = 100;
const DEFAULT_BURST = 100;

/** Scaled units in one permit. One millisecond is `ratePerSec` of them. */
const UNIT = 1000;

export default class Gcra {
  private readonly ratePerSec: number;
  private readonly burst: number;
  /** The burst tolerance, in scaled units: how far ahead of now a TAT may sit. */
  private readonly tolerance: number;
  /** Theoretical arrival time per key, in scaled units. */
  private readonly tat = new Map<number, number>();

  constructor(params: Partial<GcraParams> = {}) {
    const ratePerSec = params.ratePerSec ?? DEFAULT_RATE_PER_SEC;
    const burst = params.burst ?? DEFAULT_BURST;

    if (!Number.isInteger(ratePerSec) || ratePerSec < 1) {
      throw new RangeError(`Gcra: ratePerSec must be a positive integer, received ${ratePerSec}`);
    }
    if (!Number.isInteger(burst) || burst < 1) {
      throw new RangeError(`Gcra: burst must be a positive integer, received ${burst}`);
    }

    this.ratePerSec = ratePerSec;
    this.burst = burst;
    this.tolerance = (burst - 1) * UNIT;
  }

  /** `now` in milliseconds, as scaled units. */
  private scale(now: number): number {
    return now * this.ratePerSec;
  }

  allow(key: number, cost: number, now: number): boolean {
    // A cost above the burst can never be met. The conformance test below does
    // not cap on its own — an idle key's TAT sits arbitrarily far in the past —
    // so the ceiling has to be stated.
    if (cost > this.burst) return false;

    const scaled = this.scale(now);
    const tat = this.tat.get(key) ?? scaled;

    // Conforming when the request is no earlier than the scheduled time, less
    // whatever tolerance the burst allowance buys. Charging `cost` permits
    // means needing `cost` of them, so the tolerance shrinks accordingly.
    if (scaled < tat - this.tolerance + (cost - 1) * UNIT) return false;

    // `max` is what stops an idle key banking unbounded credit: the schedule
    // restarts from now rather than from a TAT left far in the past.
    this.tat.set(key, Math.max(scaled, tat) + cost * UNIT);
    return true;
  }

  /**
   * Milliseconds until one more permit would be admitted.
   *
   * Exact. The TAT already says when the next request is due, so the answer is
   * a subtraction and a ceiling division rather than a search.
   */
  retryAfter(key: number, now: number): number {
    const tat = this.tat.get(key);
    if (tat === undefined) return 0;

    const scaled = this.scale(now);
    const target = tat - this.tolerance;
    if (scaled >= target) return 0;

    // The first whole millisecond at or past the target.
    const at = Math.ceil(target / this.ratePerSec);
    return at - now;
  }

  /** How many keys are tracked. One integer each — the reason to use this. */
  stateSize(): number {
    return this.tat.size;
  }

  /**
   * Whole permits available to a key.
   *
   * Derived from the TAT rather than stored, which is the whole idea. It is
   * spelled the same as the token bucket's `tokensOf` so the two policies'
   * vectors can be read side by side.
   */
  tokensOf(key: number, now: number): number {
    const tat = this.tat.get(key);
    if (tat === undefined) return this.burst;

    const scaled = this.scale(now);
    const available = scaled - (tat - this.burst * UNIT);
    if (available <= 0) return 0;
    return Math.min(this.burst, Math.floor(available / UNIT));
  }
}
