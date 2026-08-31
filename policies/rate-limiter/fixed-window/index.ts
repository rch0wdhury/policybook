/**
 * Fixed window — count requests inside a clock-aligned window, reset at the edge.
 *
 * The simplest limiter that works, and the one most services start with. Divide
 * time into windows of `windowMs`, keep one counter per key, and refuse
 * anything past `limit` until the window rolls over. Two integers per key, no
 * allocation on the hot path, and a counter that a Redis `INCR` with a TTL
 * implements exactly — which is why it is everywhere.
 *
 * It has one well-known flaw, and it is not subtle: **a client can send `limit`
 * requests at the end of one window and `limit` more at the start of the next,
 * putting `2 x limit` through in an interval shorter than a single window.**
 * With a 100/s limit that is 200 requests inside two milliseconds, and the
 * downstream service feels 200, not 100. Whether that matters depends entirely
 * on what is downstream: a database connection pool cares a great deal, a
 * billing quota measured per calendar month does not.
 *
 * Windows are **aligned to the epoch**, not to a key's first request. Aligned
 * windows make the limiter stateless across processes — two servers agree on
 * which window it is without talking to each other — and that is the property
 * that makes this design distributable at all.
 */

export interface FixedWindowParams {
  /** Requests allowed per window. */
  limit: number;
  /** Window length, in milliseconds. */
  windowMs: number;
}

const DEFAULT_LIMIT = 100;
const DEFAULT_WINDOW_MS = 1_000;

/** What the limiter remembers about one key. */
interface Bucket {
  /** Start of the window this count belongs to, aligned to the epoch. */
  windowStart: number;
  count: number;
}

export default class FixedWindow {
  private readonly limit: number;
  private readonly windowMs: number;
  private readonly buckets = new Map<number, Bucket>();

  constructor(params: Partial<FixedWindowParams> = {}) {
    const limit = params.limit ?? DEFAULT_LIMIT;
    const windowMs = params.windowMs ?? DEFAULT_WINDOW_MS;

    if (!Number.isInteger(limit) || limit < 1) {
      throw new RangeError(`FixedWindow: limit must be a positive integer, received ${limit}`);
    }
    if (!Number.isInteger(windowMs) || windowMs < 1) {
      throw new RangeError(
        `FixedWindow: windowMs must be a positive integer, received ${windowMs}`,
      );
    }

    this.limit = limit;
    this.windowMs = windowMs;
  }

  /** The start of the window containing `now`. Integer division, no floats. */
  private windowOf(now: number): number {
    return now - (now % this.windowMs);
  }

  allow(key: number, cost: number, now: number): boolean {
    const windowStart = this.windowOf(now);
    let bucket = this.buckets.get(key);

    if (bucket === undefined) {
      bucket = { windowStart, count: 0 };
      this.buckets.set(key, bucket);
    } else if (bucket.windowStart !== windowStart) {
      // A new window: the old count is gone, however recently it was earned.
      // This discontinuity is the whole trade-off.
      bucket.windowStart = windowStart;
      bucket.count = 0;
    }

    if (bucket.count + cost > this.limit) return false;
    bucket.count += cost;
    return true;
  }

  /**
   * Milliseconds until one more request would be admitted.
   *
   * Exact rather than a guess: the counter resets at the window edge and
   * nothing before then can change the answer. It assumes a cost of one, which
   * is what the interface can express.
   */
  retryAfter(key: number, now: number): number {
    const bucket = this.buckets.get(key);
    if (bucket === undefined) return 0;

    const windowStart = this.windowOf(now);
    if (bucket.windowStart !== windowStart) return 0;
    if (bucket.count < this.limit) return 0;

    return windowStart + this.windowMs - now;
  }

  /**
   * How many keys are tracked.
   *
   * This never falls on its own: a key seen once is remembered forever. A real
   * deployment gives the counter a TTL of one window, which is exactly what the
   * Redis idiom does; this implementation keeps the state so the memory cost is
   * visible in the benchmark rather than hidden by a background sweep.
   */
  stateSize(): number {
    return this.buckets.size;
  }

  /** The current count for a key, for tests and vectors. */
  countOf(key: number, now: number): number {
    const bucket = this.buckets.get(key);
    if (bucket === undefined) return 0;
    return bucket.windowStart === this.windowOf(now) ? bucket.count : 0;
  }
}
