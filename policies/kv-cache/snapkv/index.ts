/**
 * SnapKV — score on the last few steps, then max-pool across neighbours.
 *
 * Two ideas, and the second is the one nothing else here has.
 *
 * **A forgetting window.** The score is the attention a position received over
 * the last `obsWindow` steps only. Anything older has no weight at all, which
 * puts this between [h2o](../h2o/), whose cumulative sum never forgets, and
 * [tova](../tova/), which remembers exactly one step.
 *
 * **A max-pool across positions.** Before choosing victims, each position's
 * score is replaced by the maximum over its `poolKernel` neighbours in the kept
 * order. Li et al. added this because selecting tokens purely on individual
 * scores fragments the context: the model attends to a *phrase*, the peak lands
 * on one token of it, and evicting the rest leaves a fragment that is worse
 * than useless. Pooling lets a high scorer defend the tokens around it.
 *
 * That is a real difference in kind rather than degree. Every other policy in
 * this domain scores each position independently; this one is the only one
 * where a position's fate depends on its neighbours.
 *
 * **On the adaptation.** SnapKV in the paper is a *prefill* algorithm: it
 * compresses a long prompt once, by looking at the attention its last few query
 * positions paid to the prompt's keys. This registry's harness is a decode-time
 * eviction loop, so the observation window here is the last `obsWindow` decode
 * steps and the pooling runs over adjacent *kept* positions — which, after
 * eviction, are not necessarily adjacent tokens. The README says so plainly;
 * the mechanism is the paper's, the setting is not.
 */

import type { KvCachePolicy } from "../../../packages/core/src/domains/kv-cache/interface.ts";

export interface SnapKvParams {
  /** Maximum token positions kept in the cache. */
  budget: number;
  /** The most recent positions, never evicted whatever their score. */
  recentWindow: number;
  /** How many recent decode steps the score is summed over. */
  obsWindow: number;
  /** Width of the max-pool across neighbouring kept positions. Must be odd. */
  poolKernel: number;
}

const DEFAULT_BUDGET = 512;
const DEFAULT_RECENT_WINDOW = 32;
const DEFAULT_OBS_WINDOW = 16;
const DEFAULT_POOL_KERNEL = 7;

export default class SnapKv implements KvCachePolicy {
  private readonly recentWindow: number;
  private readonly obsWindow: number;
  /** Half the pool kernel, rounded down: the radius on each side. */
  private readonly poolRadius: number;
  /** `budget + 1`: the most that can be held before an eviction is asked for. */
  private readonly capacity: number;
  /** Kept positions in ascending order — the order `attn` arrives in. */
  private readonly positions: Int32Array;
  /**
   * Per-position ring of the last `obsWindow` attention weights.
   *
   * Position-major (`history[i * obsWindow + slot]`), so compaction moves each
   * surviving position's whole record in one contiguous run. Stored as float32,
   * which is lossless — the weights arrive as float32 — and halves the memory.
   */
  private readonly history: Float32Array;
  /** Scratch for the eviction pass, so `evict` allocates nothing. */
  private readonly sums: Float64Array;
  private readonly pooled: Float64Array;
  private readonly doomed: Uint8Array;
  private readonly victims: number[] = [];
  private size = 0;
  /** Which ring slot the next step writes to. */
  private slot = 0;

  constructor(params: Partial<SnapKvParams> = {}) {
    const budget = params.budget ?? DEFAULT_BUDGET;
    const recentWindow = params.recentWindow ?? DEFAULT_RECENT_WINDOW;
    const obsWindow = params.obsWindow ?? DEFAULT_OBS_WINDOW;
    const poolKernel = params.poolKernel ?? DEFAULT_POOL_KERNEL;

    if (!Number.isInteger(budget) || budget < 1) {
      throw new RangeError(`SnapKv: budget must be a positive integer, received ${budget}`);
    }
    if (!Number.isInteger(recentWindow) || recentWindow < 0) {
      throw new RangeError(
        `SnapKv: recentWindow must be a non-negative integer, received ${recentWindow}`,
      );
    }
    if (recentWindow >= budget) {
      throw new RangeError(
        `SnapKv: recentWindow (${recentWindow}) must be smaller than budget (${budget}), ` +
          "or there is nothing the score is allowed to choose between.",
      );
    }
    if (!Number.isInteger(obsWindow) || obsWindow < 1) {
      throw new RangeError(
        `SnapKv: obsWindow must be a positive integer, received ${obsWindow}`,
      );
    }
    // An even kernel has no centre, so "the neighbours of position i" would be
    // lopsided and the pooling would drift in one direction.
    if (!Number.isInteger(poolKernel) || poolKernel < 1 || poolKernel % 2 === 0) {
      throw new RangeError(
        `SnapKv: poolKernel must be a positive odd integer, received ${poolKernel}`,
      );
    }

    this.recentWindow = recentWindow;
    this.obsWindow = obsWindow;
    this.poolRadius = (poolKernel - 1) / 2;
    this.capacity = budget + 1;

    this.positions = new Int32Array(this.capacity);
    this.history = new Float32Array(this.capacity * obsWindow);
    this.sums = new Float64Array(this.capacity);
    this.pooled = new Float64Array(this.capacity);
    this.doomed = new Uint8Array(this.capacity);

    // Position 0's token exists before the first decode step (see the domain
    // interface), with an empty history.
    this.positions[0] = 0;
    this.size = 1;
  }

  /**
   * Record this step's attention into the ring, evicting the oldest entry.
   *
   * Every kept position writes to the same slot, so one counter serves them
   * all, and a weight simply falls out of the window when the ring wraps onto
   * it `obsWindow` observations later.
   *
   * **A null attention vector is entirely inert** — nothing is written and the
   * ring does not advance — so the window spans the last `obsWindow` *observed*
   * steps rather than the last `obsWindow` calls. The alternative, advancing
   * without writing, would leave a stale weight sitting in the slot for another
   * full cycle, so a window that claims to cover the recent past would quietly
   * be summing values of indeterminate age. This also matches h2o and tova,
   * where a null vector likewise changes nothing.
   */
  onDecodeStep(pos: number, attn: Float32Array | null): void {
    if (attn !== null) {
      const shared = Math.min(attn.length, this.size);
      for (let i = 0; i < shared; i += 1) {
        this.history[i * this.obsWindow + this.slot] = attn[i]!;
      }
      this.slot += 1;
      if (this.slot === this.obsWindow) this.slot = 0;
    }

    if (this.size === this.capacity) {
      throw new RangeError(
        `SnapKv: asked to hold ${this.size + 1} positions with a budget of ` +
          `${this.capacity - 1}. The harness's budget must match the policy's.`,
      );
    }

    const base = this.size * this.obsWindow;
    this.history.fill(0, base, base + this.obsWindow);
    this.positions[this.size] = pos;
    this.size += 1;
  }

  /**
   * Drop the lowest-pooled-scoring positions outside the recent window.
   *
   * The returned array is reused between calls, which the domain interface
   * permits: the harness consumes it before the policy is called again.
   */
  evict(budget: number): number[] {
    this.victims.length = 0;
    const needed = this.size - budget;
    if (needed <= 0) return this.victims;

    // Sum each position's window from scratch rather than maintaining a running
    // total. Adding and subtracting from a running sum would drift, and the
    // drift would have to be bit-identical in three languages to stay
    // reproducible; summing afresh is O(obsWindow) per position and has nothing
    // to get wrong. Slot order is fixed — index 0 upward, not chronological —
    // which is arbitrary but pinned, and identical everywhere.
    for (let i = 0; i < this.size; i += 1) {
      const base = i * this.obsWindow;
      let total = 0;
      for (let s = 0; s < this.obsWindow; s += 1) total += this.history[base + s]!;
      this.sums[i] = total;
    }

    // Max-pool across neighbours, over the whole kept set: a protected recent
    // position is still a legitimate neighbour to inherit a score from.
    for (let i = 0; i < this.size; i += 1) {
      const lo = i - this.poolRadius < 0 ? 0 : i - this.poolRadius;
      const hiRaw = i + this.poolRadius;
      const hi = hiRaw >= this.size ? this.size - 1 : hiRaw;
      let best = this.sums[lo]!;
      for (let j = lo + 1; j <= hi; j += 1) {
        if (this.sums[j]! > best) best = this.sums[j]!;
      }
      this.pooled[i] = best;
    }

    const protectedCount = Math.min(this.recentWindow, this.size);
    const evictableEnd = this.size - protectedCount;

    for (let taken = 0; taken < needed && taken < evictableEnd; taken += 1) {
      let best = -1;
      for (let i = 0; i < evictableEnd; i += 1) {
        if (this.doomed[i] === 1) continue;
        // Strictly less, so a tie leaves the earlier index standing — which is
        // the lower position, since the array is ascending.
        if (best === -1 || this.pooled[i]! < this.pooled[best]!) best = i;
      }
      if (best === -1) break;
      this.doomed[best] = 1;
    }

    // One compacting pass: victims come out in ascending position order, and
    // each survivor's whole history moves with it.
    let write = 0;
    for (let read = 0; read < this.size; read += 1) {
      if (this.doomed[read] === 1) {
        this.doomed[read] = 0;
        this.victims.push(this.positions[read]!);
        continue;
      }
      if (write !== read) {
        this.positions[write] = this.positions[read]!;
        this.history.copyWithin(
          write * this.obsWindow,
          read * this.obsWindow,
          read * this.obsWindow + this.obsWindow,
        );
      }
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
   * The attention `pos` received over the observation window, unpooled.
   *
   * Reporting only — it exists so vectors can pin the windowing arithmetic
   * separately from the pooling, which is what lets a failure say which of the
   * two is wrong.
   */
  windowScoreOf(pos: number): number {
    for (let i = 0; i < this.size; i += 1) {
      if (this.positions[i] !== pos) continue;
      const base = i * this.obsWindow;
      let total = 0;
      for (let s = 0; s < this.obsWindow; s += 1) total += this.history[base + s]!;
      return total;
    }
    return -1;
  }
}
