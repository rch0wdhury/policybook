/**
 * StreamingLLM — a sliding window that also pins the first few tokens.
 *
 * Xiao et al. noticed something odd about trained transformers: a large,
 * roughly content-independent share of every attention distribution lands on
 * the first few tokens of the sequence. Not because those tokens matter, but
 * because softmax has to put its mass somewhere, and the earliest positions are
 * visible from everywhere. They called them **attention sinks**.
 *
 * The consequence is sharp. Evict the sinks and the mass they were absorbing is
 * redistributed over tokens that were never meant to carry it, and generation
 * degrades immediately — not gradually, as you would expect from losing a few
 * old tokens, but at the step the first sink goes. A
 * [sliding window](../sliding-window/) evicts them first, because they are the
 * oldest thing it holds.
 *
 * So this policy is that one plus four pinned positions, and it recovers most
 * of the quality gap for a fixed four slots and no arithmetic. It still reads
 * no attention at all, which is what separates it from
 * [h2o](../h2o/) and the other attention-aware policies: it does not find the
 * *important* old tokens, only the structurally special ones.
 *
 * The implementation is the sliding window's ring buffer, with the sinks held
 * outside it. O(1) per step, no allocation after construction.
 */

import type { KvCachePolicy } from "../../../packages/core/src/domains/kv-cache/interface.ts";

export interface StreamingLlmParams {
  /** Maximum token positions kept in the cache. */
  budget: number;
  /** How many of the first positions are pinned and never evicted. */
  sinks: number;
}

const DEFAULT_BUDGET = 512;
/**
 * Four, from the paper.
 *
 * Xiao et al. measure the recovery as a function of this and find it flat from
 * about four onward: the first token carries most of the sink mass, and by the
 * fourth there is very little left to recover.
 */
const DEFAULT_SINKS = 4;

export default class StreamingLlm implements KvCachePolicy {
  private readonly sinks: number;
  /** `budget - sinks + 1`: the most recent positions held before an eviction. */
  private readonly capacity: number;
  private readonly slots: Int32Array;
  private readonly victims: number[] = [];
  private head = 0;
  private size = 0;
  /** How many sink positions have actually been seen. */
  private sinksHeld = 0;

  constructor(params: Partial<StreamingLlmParams> = {}) {
    const budget = params.budget ?? DEFAULT_BUDGET;
    const sinks = params.sinks ?? DEFAULT_SINKS;

    if (!Number.isInteger(budget) || budget < 1) {
      throw new RangeError(
        `StreamingLlm: budget must be a positive integer, received ${budget}`,
      );
    }
    if (!Number.isInteger(sinks) || sinks < 0) {
      throw new RangeError(
        `StreamingLlm: sinks must be a non-negative integer, received ${sinks}`,
      );
    }
    // With no room left over the policy could not keep the newest token, which
    // is not a configuration anyone wants and would silently behave nothing
    // like the paper.
    if (sinks >= budget) {
      throw new RangeError(
        `StreamingLlm: sinks (${sinks}) must be smaller than budget (${budget}), ` +
          "or there is no room for a recency window.",
      );
    }

    this.sinks = sinks;
    this.capacity = budget - sinks + 1;
    this.slots = new Int32Array(this.capacity);

    // Position 0's token exists before the first decode step (see the domain
    // interface). It is a sink when any are configured, and otherwise the first
    // entry of the window.
    if (sinks > 0) {
      this.sinksHeld = 1;
    } else {
      this.slots[0] = 0;
      this.size = 1;
    }
  }

  /**
   * Note the new token, as a sink or as part of the recency window.
   *
   * Attention is not a parameter: this policy pins the *structurally* special
   * positions, not the important ones, and never looks at a weight.
   */
  onDecodeStep(pos: number): void {
    if (pos < this.sinks) {
      this.sinksHeld += 1;
      return;
    }

    if (this.size === this.capacity) {
      throw new RangeError(
        `StreamingLlm: asked to hold more than ${this.capacity - 1} window positions ` +
          "with this budget. The harness's budget must match the policy's.",
      );
    }

    let slot = this.head + this.size;
    if (slot >= this.capacity) slot -= this.capacity;
    this.slots[slot] = pos;
    this.size += 1;
  }

  /**
   * Drop the oldest non-sink positions until the budget is met.
   *
   * The returned array is reused between calls, which the domain interface
   * permits: the harness consumes it before the policy is called again.
   */
  evict(budget: number): number[] {
    this.victims.length = 0;

    while (this.sinksHeld + this.size > budget && this.size > 0) {
      this.victims.push(this.slots[this.head]!);
      this.head += 1;
      if (this.head === this.capacity) this.head = 0;
      this.size -= 1;
    }

    return this.victims;
  }

  /** How many positions are currently held, sinks included. */
  keptCount(): number {
    return this.sinksHeld + this.size;
  }

  /** How many pinned sink positions are held. Reported for tests and the site. */
  sinkCount(): number {
    return this.sinksHeld;
  }
}
