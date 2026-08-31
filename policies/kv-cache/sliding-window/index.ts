/**
 * Sliding window — keep the most recent tokens and forget the rest.
 *
 * The baseline every other policy in this domain is measured against, and a
 * much stronger one than it looks. Attention is dominated by recency: the token
 * being generated attends most heavily to the handful before it, so a policy
 * that keeps only the recent ones still retains most of the mass. Anyone
 * proposing something cleverer has to beat this first.
 *
 * What it gets wrong is everything that is old and still important. Attention
 * sinks — the first few tokens of the sequence, which real models return to
 * constantly regardless of content — are dropped almost immediately, and a
 * transformer deprived of them degrades sharply rather than gracefully. That
 * single observation is the whole of
 * [streaming-llm](../streaming-llm/), which is this policy plus four positions.
 *
 * The implementation is a ring buffer of positions: O(1) per step, no
 * allocation after construction, and exactly `budget + 1` slots because that is
 * the most that can be held before the harness asks for an eviction.
 */

import type { KvCachePolicy } from "../../../packages/core/src/domains/kv-cache/interface.ts";

export interface SlidingWindowParams {
  /** Maximum token positions kept in the cache. */
  budget: number;
}

const DEFAULT_BUDGET = 512;

export default class SlidingWindow implements KvCachePolicy {
  /** `budget + 1`: the most that can be held before an eviction is asked for. */
  private readonly capacity: number;
  /** Kept positions in arrival order, oldest at `head`. */
  private readonly slots: Int32Array;
  /** Reused across calls; see `evict`. */
  private readonly victims: number[] = [];
  private head = 0;
  private size = 0;

  constructor(params: Partial<SlidingWindowParams> = {}) {
    const budget = params.budget ?? DEFAULT_BUDGET;

    if (!Number.isInteger(budget) || budget < 1) {
      throw new RangeError(
        `SlidingWindow: budget must be a positive integer, received ${budget}`,
      );
    }

    this.capacity = budget + 1;
    this.slots = new Int32Array(this.capacity);

    // Position 0's token exists before the first decode step, so the cache
    // holds it from the outset (see the domain interface).
    this.slots[0] = 0;
    this.size = 1;
  }

  /**
   * Note the new token. Attention is not a parameter, and that is the point:
   * this policy cannot look at it, so it cannot be accused of using it.
   */
  onDecodeStep(pos: number): void {
    if (this.size === this.capacity) {
      throw new RangeError(
        `SlidingWindow: asked to hold ${this.size + 1} positions with a budget of ` +
          `${this.capacity - 1}. The harness's budget must match the policy's.`,
      );
    }

    let slot = this.head + this.size;
    if (slot >= this.capacity) slot -= this.capacity;
    this.slots[slot] = pos;
    this.size += 1;
  }

  /**
   * Drop the oldest positions until the budget is met.
   *
   * The returned array is reused between calls, which the domain interface
   * permits: the harness consumes it before the policy is called again.
   */
  evict(budget: number): number[] {
    this.victims.length = 0;

    while (this.size > budget) {
      this.victims.push(this.slots[this.head]!);
      this.head += 1;
      if (this.head === this.capacity) this.head = 0;
      this.size -= 1;
    }

    return this.victims;
  }

  /** How many positions are currently held. Reported for tests and the site. */
  keptCount(): number {
    return this.size;
  }
}
