/**
 * Sliding window log — remember every request time, and count the recent ones.
 *
 * The only limiter here that enforces its limit *exactly*. It keeps the
 * timestamp of every admitted request in a ring buffer per key, drops the ones
 * that have aged out, and admits if fewer than `limit` remain. Over any window
 * of `windowMs` ending at any instant, the number of admitted requests is at
 * most `limit` — no boundary effect, no estimate, no burst that slips through.
 *
 * That exactness is bought with memory, and the price is the whole story: the
 * ring holds `limit` timestamps **per key**, so a 100/s limit over a million
 * keys is a hundred million timestamps. [Fixed window](../fixed-window/) needs
 * two integers per key and [GCRA](../gcra/) needs one; this needs a hundred.
 * Use it when the limit is small, the keys are few, or the exactness is worth
 * paying for — an expensive downstream call, a hard contractual quota — and use
 * something else otherwise.
 *
 * Expiry is O(1) amortized: each timestamp is written once and dropped once, so
 * the loop that clears the front of the ring does constant work per admitted
 * request no matter how bursty the arrivals are.
 */

export interface SlidingLogParams {
  /** Requests allowed in any window of `windowMs`. */
  limit: number;
  /** Window length, in milliseconds. */
  windowMs: number;
}

const DEFAULT_LIMIT = 100;
const DEFAULT_WINDOW_MS = 1_000;

/**
 * One key's ring of admitted timestamps.
 *
 * `head` is the oldest entry and `count` how many are live; the ring holds
 * exactly `limit` slots because that is the most that can ever be in the window
 * at once.
 */
interface Log {
  /**
   * Timestamps in milliseconds. `Float64Array` rather than `Uint32Array`
   * because a millisecond clock passes 2^32 after 49 days, and a limiter should
   * not develop a fault on day 50. A double holds every integer below 2^53
   * exactly, so this is still integer arithmetic.
   */
  times: Float64Array;
  head: number;
  count: number;
}

export default class SlidingLog {
  private readonly limit: number;
  private readonly windowMs: number;
  private readonly logs = new Map<number, Log>();

  constructor(params: Partial<SlidingLogParams> = {}) {
    const limit = params.limit ?? DEFAULT_LIMIT;
    const windowMs = params.windowMs ?? DEFAULT_WINDOW_MS;

    if (!Number.isInteger(limit) || limit < 1) {
      throw new RangeError(`SlidingLog: limit must be a positive integer, received ${limit}`);
    }
    if (!Number.isInteger(windowMs) || windowMs < 1) {
      throw new RangeError(
        `SlidingLog: windowMs must be a positive integer, received ${windowMs}`,
      );
    }

    this.limit = limit;
    this.windowMs = windowMs;
  }

  /**
   * Drop every timestamp that has left the window.
   *
   * The window is `(now - windowMs, now]`: a request exactly `windowMs` old has
   * left it. That choice makes the guarantee "at most `limit` in any `windowMs`
   * interval" true as stated rather than off by one at the edge.
   */
  private expire(log: Log, now: number): void {
    const cutoff = now - this.windowMs;
    while (log.count > 0 && log.times[log.head]! <= cutoff) {
      log.head = (log.head + 1) % this.limit;
      log.count -= 1;
    }
  }

  allow(key: number, cost: number, now: number): boolean {
    let log = this.logs.get(key);
    if (log === undefined) {
      log = { times: new Float64Array(this.limit), head: 0, count: 0 };
      this.logs.set(key, log);
    }

    this.expire(log, now);
    if (log.count + cost > this.limit) return false;

    // A request costing n occupies n slots: it is n requests as far as the
    // limit is concerned, and they all age out together.
    for (let unit = 0; unit < cost; unit += 1) {
      log.times[(log.head + log.count) % this.limit] = now;
      log.count += 1;
    }
    return true;
  }

  /**
   * Milliseconds until one more request would be admitted.
   *
   * Exact for a cost of one: the log is full, so admission waits on the oldest
   * entry leaving the window, and nothing that happens before then can change
   * that.
   */
  retryAfter(key: number, now: number): number {
    const log = this.logs.get(key);
    if (log === undefined) return 0;

    this.expire(log, now);
    if (log.count < this.limit) return 0;

    // The oldest entry leaves the window at `time + windowMs`, because the
    // window excludes its far end — so that instant is when room appears, not
    // the millisecond after it.
    return log.times[log.head]! + this.windowMs - now;
  }

  /** How many keys are tracked. Each costs `limit` timestamps. */
  stateSize(): number {
    return this.logs.size;
  }

  /** Live entries in a key's window, for tests and vectors. */
  countOf(key: number, now: number): number {
    const log = this.logs.get(key);
    if (log === undefined) return 0;
    this.expire(log, now);
    return log.count;
  }
}
