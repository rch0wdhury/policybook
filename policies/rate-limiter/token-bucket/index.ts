/**
 * Token bucket — spend from a balance that refills at a steady rate.
 *
 * The default rate limiter, and the one to reach for unless you know why you
 * want something else. A key holds up to `burst` tokens; each request spends
 * one; the balance refills at `ratePerSec` and stops at `burst`. That single
 * rule gives you the two properties most services actually want: a long-run
 * ceiling of `ratePerSec`, and the ability for a caller who has been quiet to
 * spend what it saved.
 *
 * Against the window policies its advantage is that it has **no window**. A
 * [fixed window](../fixed-window/) has a seam where twice the limit slips
 * through; a [sliding counter](../sliding-counter/) removes the seam but still
 * treats a caller's allowance as something that refills in one lump when the
 * window turns. A token bucket refills continuously, so a caller that is
 * refused learns exactly how long to wait, and the answer is never "up to a
 * whole window".
 *
 * **The ledger is integer arithmetic**. Tokens
 * are whole; the fractional part lives in a separate `credit` accumulator
 * measured in thousandths of a token:
 *
 *     credit += ratePerSec * elapsedMs
 *     tokens += credit div 1000
 *     credit  = credit mod 1000
 *
 * A floating-point balance would drift differently in three languages, and the
 * drift would eventually change a decision.
 */

export interface TokenBucketParams {
  /** Tokens added per second. */
  ratePerSec: number;
  /** Maximum tokens a key can hold, and so the largest burst it can spend. */
  burst: number;
}

const DEFAULT_RATE_PER_SEC = 100;
const DEFAULT_BURST = 100;

/** One key's balance. */
interface Bucket {
  tokens: number;
  /** Thousandths of a token, carried between refills. Always 0..999. */
  credit: number;
  last: number;
}

export default class TokenBucket {
  private readonly ratePerSec: number;
  private readonly burst: number;
  /**
   * How long a completely empty bucket takes to fill.
   *
   * Idle time is clamped to this before the multiply. Without it a key untouched
   * for a month would compute `ratePerSec * elapsed` in the billions — harmless
   * in JavaScript, an overflow in C — and the result is identical either way,
   * because the bucket saturates long before.
   */
  private readonly fillMs: number;
  private readonly buckets = new Map<number, Bucket>();

  constructor(params: Partial<TokenBucketParams> = {}) {
    const ratePerSec = params.ratePerSec ?? DEFAULT_RATE_PER_SEC;
    const burst = params.burst ?? DEFAULT_BURST;

    if (!Number.isInteger(ratePerSec) || ratePerSec < 1) {
      throw new RangeError(
        `TokenBucket: ratePerSec must be a positive integer, received ${ratePerSec}`,
      );
    }
    if (!Number.isInteger(burst) || burst < 1) {
      throw new RangeError(`TokenBucket: burst must be a positive integer, received ${burst}`);
    }

    this.ratePerSec = ratePerSec;
    this.burst = burst;
    this.fillMs = Math.ceil((burst * 1000) / ratePerSec);
  }

  /** Bring a bucket up to date at `now`. */
  private refill(bucket: Bucket, now: number): void {
    let elapsed = now - bucket.last;
    if (elapsed <= 0) return;
    if (elapsed > this.fillMs) elapsed = this.fillMs;

    bucket.credit += this.ratePerSec * elapsed;
    bucket.tokens += Math.floor(bucket.credit / 1000);
    bucket.credit %= 1000;

    if (bucket.tokens >= this.burst) {
      // The bucket overflows: tokens above `burst` are lost, and so is the
      // fraction that would have become the next one.
      bucket.tokens = this.burst;
      bucket.credit = 0;
    }
    bucket.last = now;
  }

  private bucketFor(key: number, now: number): Bucket {
    let bucket = this.buckets.get(key);
    if (bucket === undefined) {
      // A key never seen starts full. It has been idle for all of history, and
      // a bucket that started empty would refuse a first request for no reason
      // a caller could act on.
      bucket = { tokens: this.burst, credit: 0, last: now };
      this.buckets.set(key, bucket);
      return bucket;
    }
    this.refill(bucket, now);
    return bucket;
  }

  allow(key: number, cost: number, now: number): boolean {
    const bucket = this.bucketFor(key, now);
    if (bucket.tokens < cost) return false;
    bucket.tokens -= cost;
    return true;
  }

  /**
   * Milliseconds until one more token exists.
   *
   * Exact, and the reason a token bucket is pleasant to build on: the answer
   * comes from the ledger rather than from a window edge, so it is usually a
   * few milliseconds rather than "wait for the window to turn".
   */
  retryAfter(key: number, now: number): number {
    const bucket = this.buckets.get(key);
    if (bucket === undefined) return 0;

    this.refill(bucket, now);
    if (bucket.tokens >= 1) return 0;

    // Ceiling division: the token arrives at the first whole millisecond where
    // the credit reaches 1,000.
    const deficit = 1000 - bucket.credit;
    return Math.floor((deficit + this.ratePerSec - 1) / this.ratePerSec);
  }

  /** How many keys are tracked. Three integers each. */
  stateSize(): number {
    return this.buckets.size;
  }

  /** Whole tokens available to a key, for tests and vectors. */
  tokensOf(key: number, now: number): number {
    const bucket = this.buckets.get(key);
    if (bucket === undefined) return this.burst;
    this.refill(bucket, now);
    return bucket.tokens;
  }
}
