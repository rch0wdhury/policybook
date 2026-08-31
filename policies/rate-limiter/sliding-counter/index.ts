/**
 * Sliding window counter — two fixed windows, weighted by how far into the new
 * one you are.
 *
 * The practical compromise between [fixed window](../fixed-window/) and
 * [sliding log](../sliding-log/), and what most production limiters actually
 * run. Keep the count for the current window and the previous one, then
 * estimate the rate over the trailing window by fading the old count out as the
 * new window fills:
 *
 *     estimate = previous * (windowMs - elapsed) / windowMs + current
 *
 * Three integers per key instead of a hundred timestamps, and the boundary
 * burst is gone: a client that spent its whole budget just before the edge
 * finds that budget still counted against it just after, decaying smoothly to
 * nothing over the following window.
 *
 * It is an **estimate**, and it is worth being precise about which way it errs.
 * The formula assumes the previous window's requests were spread evenly across
 * it. If they were actually all at the start, they have really left the
 * trailing window and the limiter refuses requests it could have allowed. If
 * they were all at the end, the limiter lets slightly more through than the
 * limit. Cloudflare's published analysis of this scheme on real traffic found
 * the error negligible — well under one percent of requests misclassified —
 * which is why the memory saving usually wins.
 *
 * **The weighting is integer arithmetic**: `previous * (windowMs - elapsed)`
 * divided by `windowMs` with the remainder discarded. Flooring makes the estimate err very slightly low, and it makes
 * the three language ports produce identical decisions rather than diverging on
 * the last bit of a double.
 */

export interface SlidingCounterParams {
  /** Requests allowed per window. */
  limit: number;
  /** Window length, in milliseconds. */
  windowMs: number;
}

const DEFAULT_LIMIT = 100;
const DEFAULT_WINDOW_MS = 1_000;

/** What the limiter remembers about one key: two counts and which window. */
interface Counter {
  /** Start of the current window, aligned to the epoch. */
  windowStart: number;
  current: number;
  previous: number;
}

export default class SlidingCounter {
  private readonly limit: number;
  private readonly windowMs: number;
  private readonly counters = new Map<number, Counter>();

  constructor(params: Partial<SlidingCounterParams> = {}) {
    const limit = params.limit ?? DEFAULT_LIMIT;
    const windowMs = params.windowMs ?? DEFAULT_WINDOW_MS;

    if (!Number.isInteger(limit) || limit < 1) {
      throw new RangeError(
        `SlidingCounter: limit must be a positive integer, received ${limit}`,
      );
    }
    if (!Number.isInteger(windowMs) || windowMs < 1) {
      throw new RangeError(
        `SlidingCounter: windowMs must be a positive integer, received ${windowMs}`,
      );
    }

    this.limit = limit;
    this.windowMs = windowMs;
  }

  /** The start of the window containing `now`. Integer division, no floats. */
  private windowOf(now: number): number {
    return now - (now % this.windowMs);
  }

  /** Roll the counter forward to the window containing `now`. */
  private advance(counter: Counter, windowStart: number): void {
    if (counter.windowStart === windowStart) return;

    if (windowStart === counter.windowStart + this.windowMs) {
      // The very next window: today's count becomes yesterday's.
      counter.previous = counter.current;
    } else {
      // A gap of two windows or more — nothing from before is still in the
      // trailing window, so both counts go.
      counter.previous = 0;
    }
    counter.current = 0;
    counter.windowStart = windowStart;
  }

  /**
   * The weighted estimate of requests in the trailing window.
   *
   * `previous * (windowMs - elapsed) div windowMs + current`, floored.
   */
  private estimate(counter: Counter, now: number): number {
    const elapsed = now - counter.windowStart;
    const carried = Math.floor((counter.previous * (this.windowMs - elapsed)) / this.windowMs);
    return carried + counter.current;
  }

  allow(key: number, cost: number, now: number): boolean {
    const windowStart = this.windowOf(now);
    let counter = this.counters.get(key);

    if (counter === undefined) {
      counter = { windowStart, current: 0, previous: 0 };
      this.counters.set(key, counter);
    } else {
      this.advance(counter, windowStart);
    }

    if (this.estimate(counter, now) + cost > this.limit) return false;
    counter.current += cost;
    return true;
  }

  /**
   * Milliseconds until one more request would be admitted.
   *
   * Solved rather than searched. Admission needs the carried part of the
   * estimate to fall to `limit - current - 1` or below, and since it decays
   * linearly the smallest `elapsed` that achieves it follows directly:
   *
   *     carried <= need  <=>  previous * (windowMs - elapsed) < (need + 1) * windowMs
   *                      <=>  elapsed > windowMs * (previous - need - 1) / previous
   *
   * When even an empty previous window would not help — the current window has
   * already reached the limit by itself — the answer is the window edge, and
   * that one is genuinely a hint: at the edge the current count becomes the
   * previous count and may still refuse.
   */
  retryAfter(key: number, now: number): number {
    const counter = this.counters.get(key);
    if (counter === undefined) return 0;

    const windowStart = this.windowOf(now);
    this.advance(counter, windowStart);
    if (this.estimate(counter, now) < this.limit) return 0;

    const need = this.limit - counter.current - 1;
    if (need < 0) {
      // The current window has reached the limit on its own, so the wait runs
      // past the edge — but *not* only to the edge. At the edge this count
      // becomes the previous count and, undecayed, still refuses. `allow` never
      // lets `current` exceed `limit`, so it is exactly at the limit here, and
      // a single millisecond of decay is always enough to drop the carried
      // value by one. Returning the bare edge was a real bug: it made the hint
      // wrong on four denials in five.
      return windowStart + this.windowMs - now + 1;
    }
    if (counter.previous === 0) return 0;

    // `excess == 0` means the carried count is exactly one too high, which
    // still needs a millisecond of decay — so only a negative excess admits
    // immediately. Treating zero as "no wait" was a real bug here: it reported
    // retryAfter 0 for a request the very next call refused.
    const excess = counter.previous - need - 1;
    if (excess < 0) return 0;

    const target = Math.floor((this.windowMs * excess) / counter.previous) + 1;
    const elapsed = now - windowStart;
    return target > elapsed ? target - elapsed : 0;
  }

  /** How many keys are tracked. Three integers each. */
  stateSize(): number {
    return this.counters.size;
  }

  /** The weighted estimate for a key, for tests and vectors. */
  estimateOf(key: number, now: number): number {
    const counter = this.counters.get(key);
    if (counter === undefined) return 0;
    this.advance(counter, this.windowOf(now));
    return this.estimate(counter, now);
  }
}
