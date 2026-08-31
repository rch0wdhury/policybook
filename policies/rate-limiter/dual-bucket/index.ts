/**
 * Dual bucket — two limits at once, and a request must satisfy both.
 *
 * This is the shape every LLM API uses, and increasingly every metered API:
 * a **requests per minute** ceiling and a **tokens per minute** ceiling, checked
 * together. One dimension counts calls, the other counts how much work each
 * call asks for, and either can refuse on its own.
 *
 * The two limits fail in opposite directions, which is the point of having
 * both. A flood of tiny requests exhausts RPM while TPM sits nearly untouched —
 * that is a client in a retry loop, and the request ceiling is what stops it. A
 * single enormous request exhausts TPM while RPM has barely moved — that is a
 * caller submitting a book, and the token ceiling is what stops it. A limiter
 * with only one of these is blind to half the ways it can be overwhelmed.
 *
 * Each dimension is a [token bucket](../token-bucket/) with a per-minute
 * period, and everything true of that policy is true of each half here: an
 * integer ledger with a carry, continuous refill, no window seam.
 *
 * **The charge is atomic.** If either dimension would refuse, neither is
 * charged. A caller refused for tokens has not quietly spent a request too, so
 * retrying costs it nothing it did not already owe. Getting this wrong is a
 * real and easy bug: it makes a client that retries into a client that is
 * throttled harder for retrying.
 */

export interface DualBucketParams {
  /** Calls allowed per minute, regardless of size. */
  requestsPerMin: number;
  /** Units of work allowed per minute, summed over calls. */
  tokensPerMin: number;
}

const DEFAULT_REQUESTS_PER_MIN = 500;
const DEFAULT_TOKENS_PER_MIN = 200_000;

/** Both ceilings are stated per minute, so the ledger's period is a minute. */
const PERIOD_MS = 60_000;

/** One key's two ledgers, plus when they were last brought up to date. */
interface Buckets {
  requests: number;
  requestCredit: number;
  tokens: number;
  tokenCredit: number;
  last: number;
}

export default class DualBucket {
  private readonly requestsPerMin: number;
  private readonly tokensPerMin: number;
  private readonly buckets = new Map<number, Buckets>();

  constructor(params: Partial<DualBucketParams> = {}) {
    const requestsPerMin = params.requestsPerMin ?? DEFAULT_REQUESTS_PER_MIN;
    const tokensPerMin = params.tokensPerMin ?? DEFAULT_TOKENS_PER_MIN;

    if (!Number.isInteger(requestsPerMin) || requestsPerMin < 1) {
      throw new RangeError(
        `DualBucket: requestsPerMin must be a positive integer, received ${requestsPerMin}`,
      );
    }
    if (!Number.isInteger(tokensPerMin) || tokensPerMin < 1) {
      throw new RangeError(
        `DualBucket: tokensPerMin must be a positive integer, received ${tokensPerMin}`,
      );
    }

    this.requestsPerMin = requestsPerMin;
    this.tokensPerMin = tokensPerMin;
  }

  /**
   * Bring both ledgers up to date at `now`.
   *
   * The same integer ledger the token bucket uses, with a period of a minute
   * instead of a second: the fraction lives in a credit accumulator measured in
   * `PERIOD_MS`ths of a permit, so nothing is ever rounded away. Elapsed time is
   * clamped to one period, which is exactly how long a drained bucket takes to
   * refill — beyond that the result cannot change, and clamping keeps the
   * multiply small enough for the C port's arithmetic.
   */
  private refill(bucket: Buckets, now: number): void {
    let elapsed = now - bucket.last;
    if (elapsed <= 0) return;
    if (elapsed > PERIOD_MS) elapsed = PERIOD_MS;

    bucket.requestCredit += this.requestsPerMin * elapsed;
    bucket.requests += Math.floor(bucket.requestCredit / PERIOD_MS);
    bucket.requestCredit %= PERIOD_MS;
    if (bucket.requests >= this.requestsPerMin) {
      bucket.requests = this.requestsPerMin;
      bucket.requestCredit = 0;
    }

    bucket.tokenCredit += this.tokensPerMin * elapsed;
    bucket.tokens += Math.floor(bucket.tokenCredit / PERIOD_MS);
    bucket.tokenCredit %= PERIOD_MS;
    if (bucket.tokens >= this.tokensPerMin) {
      bucket.tokens = this.tokensPerMin;
      bucket.tokenCredit = 0;
    }

    bucket.last = now;
  }

  private bucketFor(key: number, now: number): Buckets {
    let bucket = this.buckets.get(key);
    if (bucket === undefined) {
      bucket = {
        requests: this.requestsPerMin,
        requestCredit: 0,
        tokens: this.tokensPerMin,
        tokenCredit: 0,
        last: now,
      };
      this.buckets.set(key, bucket);
      return bucket;
    }
    this.refill(bucket, now);
    return bucket;
  }

  /**
   * May a call costing `cost` units of work proceed?
   *
   * The call always charges one request; `cost` is what it charges the token
   * dimension. Both must be affordable, and both are charged or neither is.
   */
  allow(key: number, cost: number, now: number): boolean {
    const bucket = this.bucketFor(key, now);

    if (bucket.requests < 1) return false;
    if (bucket.tokens < cost) return false;

    bucket.requests -= 1;
    bucket.tokens -= cost;
    return true;
  }

  /**
   * Milliseconds until a smallest possible call — one request, one token —
   * would be admitted.
   *
   * The later of the two dimensions, because a caller has to satisfy both. It
   * cannot account for the size of the call you actually intend to make: the
   * interface has no cost argument, so a large call may still be refused after
   * this elapses. A caller with a specific request in mind should ask again.
   */
  retryAfter(key: number, now: number): number {
    const bucket = this.buckets.get(key);
    if (bucket === undefined) return 0;

    this.refill(bucket, now);
    return Math.max(
      waitFor(bucket.requests, bucket.requestCredit, this.requestsPerMin),
      waitFor(bucket.tokens, bucket.tokenCredit, this.tokensPerMin),
    );
  }

  /** How many keys are tracked. Five integers each. */
  stateSize(): number {
    return this.buckets.size;
  }

  /** Requests left for a key this minute, for tests and vectors. */
  requestsOf(key: number, now: number): number {
    const bucket = this.buckets.get(key);
    if (bucket === undefined) return this.requestsPerMin;
    this.refill(bucket, now);
    return bucket.requests;
  }

  /** Work units left for a key this minute, for tests and vectors. */
  tokensOf(key: number, now: number): number {
    const bucket = this.buckets.get(key);
    if (bucket === undefined) return this.tokensPerMin;
    this.refill(bucket, now);
    return bucket.tokens;
  }
}

/** Milliseconds until one whole permit accrues on a dimension. */
function waitFor(available: number, credit: number, ratePerMin: number): number {
  if (available >= 1) return 0;
  const deficit = PERIOD_MS - credit;
  return Math.floor((deficit + ratePerMin - 1) / ratePerMin);
}
