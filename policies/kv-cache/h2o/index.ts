/**
 * H2O — keep the tokens that have received the most attention so far.
 *
 * The first policy in this domain that actually reads the attention weights,
 * and the step up from [streaming-llm](../streaming-llm/). That policy pins the
 * *structurally* special positions — the first few, which absorb attention
 * regardless of content. This one finds the *important* ones: a fact stated
 * once in the middle of a long document is exactly what a recency-plus-sinks
 * policy discards and what this policy keeps.
 *
 * Zhang et al. observed that attention is not merely sparse but persistently
 * sparse: a small set of positions accumulates most of the attention across the
 * whole generation, and which positions those are is fairly stable. They called
 * them **heavy hitters**, and the policy follows directly — sum each position's
 * attention over time, and when the cache is full, evict the smallest sum.
 *
 * A recent window is held back from the scoring, because a token generated two
 * steps ago has had no opportunity to accumulate anything and would be evicted
 * immediately on a technicality. That is not a refinement, it is load-bearing:
 * without it the policy evicts the newest token at every step and the cache
 * never advances.
 *
 * **What it gets wrong** is that a cumulative sum never forgets. A position
 * that was heavily attended early and is now irrelevant keeps its score
 * forever, and defends its slot against tokens that matter now.
 * [scissorhands](../scissorhands/) counts *how often* a position mattered
 * instead, which decays that advantage; the distinguishing vectors in both
 * policies run the same steps and split precisely there.
 */

import type { KvCachePolicy } from "../../../packages/core/src/domains/kv-cache/interface.ts";

export interface H2oParams {
  /** Maximum token positions kept in the cache. */
  budget: number;
  /** The most recent positions, never evicted whatever their score. */
  recentWindow: number;
}

const DEFAULT_BUDGET = 512;
/**
 * Thirty-two, following the paper's split between heavy hitters and recency.
 *
 * The exact number matters less than that it is not zero: a token generated
 * this step has accumulated nothing, and without protection would be the first
 * thing evicted every time.
 */
const DEFAULT_RECENT_WINDOW = 32;

export default class H2o implements KvCachePolicy {
  private readonly recentWindow: number;
  /** `budget + 1`: the most that can be held before an eviction is asked for. */
  private readonly capacity: number;
  /** Kept positions in ascending order — the order `attn` arrives in. */
  private readonly positions: Int32Array;
  /** Cumulative attention per kept position, parallel to `positions`. */
  private readonly scores: Float64Array;
  /** Scratch for the eviction pass, so `evict` allocates nothing. */
  private readonly doomed: Uint8Array;
  private readonly victims: number[] = [];
  private size = 0;

  constructor(params: Partial<H2oParams> = {}) {
    const budget = params.budget ?? DEFAULT_BUDGET;
    const recentWindow = params.recentWindow ?? DEFAULT_RECENT_WINDOW;

    if (!Number.isInteger(budget) || budget < 1) {
      throw new RangeError(`H2o: budget must be a positive integer, received ${budget}`);
    }
    if (!Number.isInteger(recentWindow) || recentWindow < 0) {
      throw new RangeError(
        `H2o: recentWindow must be a non-negative integer, received ${recentWindow}`,
      );
    }
    // With the whole budget protected there would be nothing left to evict and
    // the policy could never reach its budget at all.
    if (recentWindow >= budget) {
      throw new RangeError(
        `H2o: recentWindow (${recentWindow}) must be smaller than budget (${budget}), ` +
          "or there is nothing the score is allowed to choose between.",
      );
    }

    this.recentWindow = recentWindow;
    this.capacity = budget + 1;
    this.positions = new Int32Array(this.capacity);
    this.scores = new Float64Array(this.capacity);
    this.doomed = new Uint8Array(this.capacity);

    // Position 0's token exists before the first decode step (see the domain
    // interface), and starts with no attention to its name.
    this.positions[0] = 0;
    this.scores[0] = 0;
    this.size = 1;
  }

  /**
   * Add this step's attention to each kept position's running total.
   *
   * `attn[i]` belongs to the i-th kept position in ascending order, which is
   * exactly how `positions` is stored — so the two are index-aligned and no
   * lookup is needed.
   *
   * Scores accumulate in float64 while the weights arrive as float32. The sum
   * is therefore exact for far longer than accumulating in float32 would be,
   * and identical across the three implementations.
   */
  onDecodeStep(pos: number, attn: Float32Array | null): void {
    if (attn !== null) {
      const shared = Math.min(attn.length, this.size);
      for (let i = 0; i < shared; i += 1) {
        this.scores[i] = this.scores[i]! + attn[i]!;
      }
    }

    if (this.size === this.capacity) {
      throw new RangeError(
        `H2o: asked to hold ${this.size + 1} positions with a budget of ` +
          `${this.capacity - 1}. The harness's budget must match the policy's.`,
      );
    }

    this.positions[this.size] = pos;
    this.scores[this.size] = 0;
    this.size += 1;
  }

  /**
   * Drop the lowest-scoring positions outside the recent window.
   *
   * The returned array is reused between calls, which the domain interface
   * permits: the harness consumes it before the policy is called again.
   */
  evict(budget: number): number[] {
    this.victims.length = 0;
    const needed = this.size - budget;
    if (needed <= 0) return this.victims;

    // The recent window is the tail of the ascending array, so everything
    // before `evictableEnd` is fair game and nothing after it is.
    const protectedCount = Math.min(this.recentWindow, this.size);
    const evictableEnd = this.size - protectedCount;

    // Repeated argmin rather than a sort: in steady state exactly one position
    // goes per step, so this is a single linear scan, and it needs no sort
    // implementation to mirror in C.
    for (let taken = 0; taken < needed && taken < evictableEnd; taken += 1) {
      let best = -1;
      for (let i = 0; i < evictableEnd; i += 1) {
        if (this.doomed[i] === 1) continue;
        // Strictly less, so a tie leaves the earlier index standing — which is
        // the lower position, since the array is ascending.
        if (best === -1 || this.scores[i]! < this.scores[best]!) best = i;
      }
      if (best === -1) break;
      this.doomed[best] = 1;
    }

    // One compacting pass: victims come out in ascending position order.
    let write = 0;
    for (let read = 0; read < this.size; read += 1) {
      if (this.doomed[read] === 1) {
        this.doomed[read] = 0;
        this.victims.push(this.positions[read]!);
        continue;
      }
      this.positions[write] = this.positions[read]!;
      this.scores[write] = this.scores[read]!;
      write += 1;
    }
    this.size = write;

    return this.victims;
  }

  /** How many positions are currently held. Reported for tests and the site. */
  keptCount(): number {
    return this.size;
  }

  /**
   * The cumulative attention `pos` has received, or -1 if it is not held.
   *
   * Reporting only — it exists so vectors can pin the score arithmetic
   * directly rather than inferring it from which position was evicted.
   */
  scoreOf(pos: number): number {
    for (let i = 0; i < this.size; i += 1) {
      if (this.positions[i] === pos) return this.scores[i]!;
    }
    return -1;
  }
}
