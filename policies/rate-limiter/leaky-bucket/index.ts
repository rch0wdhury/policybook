/**
 * Leaky bucket — a level that rises with each request and drains at a steady rate.
 *
 * The meter formulation: every admitted request adds one unit to a bucket that
 * leaks continuously at `ratePerSec`, and a request is refused when it would
 * overflow `capacity`. It is the classic way to describe a limiter whose job is
 * to *smooth* traffic rather than to permit a burst, and its natural
 * configuration is a small capacity — at `capacity` 1 it enforces even spacing,
 * admitting one request every `1000 / ratePerSec` milliseconds and no two
 * together.
 *
 * **At equal parameters this is the token bucket, exactly.** Substituting
 * `tokens = capacity - level` turns every line of one into the corresponding
 * line of the other: a request that fits under the ceiling is a request with a
 * token to spend, draining is refilling, and the bucket bottoming out at zero
 * is the balance saturating at `burst`. Neither is an approximation of the
 * other and neither is faster; a vector in each policy pins the equivalence,
 * and `bucket-policies.test.ts` checks it over a long random trace.
 *
 * So the choice between them is about the vocabulary your system already uses,
 * and about which parameter you find it natural to state. If you think in "how
 * big a burst may a caller spend?", the [token bucket](../token-bucket/) says
 * it directly. If you think in "how much may queue up before I shed load?",
 * this does. The defaults differ accordingly — `capacity` is 1 here, because a
 * caller who wants a burst allowance is describing a token bucket.
 *
 * **The ledger is integer arithmetic**. The
 * level is whole; the fractional part of the drain lives in a `credit`
 * accumulator measured in thousandths of a unit.
 */

export interface LeakyBucketParams {
  /** Units drained per second. */
  ratePerSec: number;
  /** Maximum level, and so the largest burst that fits at once. */
  capacity: number;
}

const DEFAULT_RATE_PER_SEC = 100;
const DEFAULT_CAPACITY = 1;

/** One key's bucket. */
interface Bucket {
  level: number;
  /** Thousandths of a unit drained, carried between updates. Always 0..999. */
  credit: number;
  last: number;
}

export default class LeakyBucket {
  private readonly ratePerSec: number;
  private readonly capacity: number;
  /**
   * How long a completely full bucket takes to drain.
   *
   * Idle time is clamped to this before the multiply, so a key untouched for a
   * month cannot overflow the arithmetic in the C port. The result is identical
   * either way, because the bucket empties long before.
   */
  private readonly drainMs: number;
  private readonly buckets = new Map<number, Bucket>();

  constructor(params: Partial<LeakyBucketParams> = {}) {
    const ratePerSec = params.ratePerSec ?? DEFAULT_RATE_PER_SEC;
    const capacity = params.capacity ?? DEFAULT_CAPACITY;

    if (!Number.isInteger(ratePerSec) || ratePerSec < 1) {
      throw new RangeError(
        `LeakyBucket: ratePerSec must be a positive integer, received ${ratePerSec}`,
      );
    }
    if (!Number.isInteger(capacity) || capacity < 1) {
      throw new RangeError(
        `LeakyBucket: capacity must be a positive integer, received ${capacity}`,
      );
    }

    this.ratePerSec = ratePerSec;
    this.capacity = capacity;
    this.drainMs = Math.ceil((capacity * 1000) / ratePerSec);
  }

  /** Let a bucket leak up to `now`. */
  private drain(bucket: Bucket, now: number): void {
    let elapsed = now - bucket.last;
    if (elapsed <= 0) return;
    if (elapsed > this.drainMs) elapsed = this.drainMs;

    bucket.credit += this.ratePerSec * elapsed;
    const drained = Math.floor(bucket.credit / 1000);
    bucket.credit %= 1000;

    if (drained >= bucket.level) {
      // The bucket has run dry. Nothing further leaks, and the fraction that
      // would have leaked next is discarded.
      bucket.level = 0;
      bucket.credit = 0;
    } else {
      bucket.level -= drained;
    }
    bucket.last = now;
  }

  private bucketFor(key: number, now: number): Bucket {
    let bucket = this.buckets.get(key);
    if (bucket === undefined) {
      // A key never seen starts empty: it has been draining for all of history.
      bucket = { level: 0, credit: 0, last: now };
      this.buckets.set(key, bucket);
      return bucket;
    }
    this.drain(bucket, now);
    return bucket;
  }

  allow(key: number, cost: number, now: number): boolean {
    const bucket = this.bucketFor(key, now);
    if (bucket.level + cost > this.capacity) return false;
    bucket.level += cost;
    return true;
  }

  /**
   * Milliseconds until one unit of room exists.
   *
   * Exact, for the same reason the token bucket's is: the level falls
   * continuously, so the answer comes from the ledger rather than from a window
   * edge.
   */
  retryAfter(key: number, now: number): number {
    const bucket = this.buckets.get(key);
    if (bucket === undefined) return 0;

    this.drain(bucket, now);
    if (bucket.level < this.capacity) return 0;

    const deficit = 1000 - bucket.credit;
    return Math.floor((deficit + this.ratePerSec - 1) / this.ratePerSec);
  }

  /** How many keys are tracked. Three integers each. */
  stateSize(): number {
    return this.buckets.size;
  }

  /** The current level for a key, for tests and vectors. */
  levelOf(key: number, now: number): number {
    const bucket = this.buckets.get(key);
    if (bucket === undefined) return 0;
    this.drain(bucket, now);
    return bucket.level;
  }
}
