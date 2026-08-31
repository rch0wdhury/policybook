/**
 * PyramidKV — spend more cache on early layers than late ones.
 *
 * Every other policy in this domain decides *which* tokens to keep. This one
 * decides *how many*, and leaves the choosing to a [snapkv](../snapkv/)-style
 * rule underneath.
 *
 * Cai et al. observed what they called pyramidal information funnelling:
 * attention in the early layers of a transformer is broad and fairly uniform,
 * spread across the whole context, while in later layers it concentrates
 * sharply onto a few positions. A uniform per-layer cache budget therefore
 * spends the same on a layer that needs to see everything and a layer that
 * needs to see a handful of tokens — overfeeding the deep layers and starving
 * the shallow ones.
 *
 * So the budget is redistributed: an arithmetic sequence across layers, with
 * the first getting `pyramidRatio` times the last, and the average preserved.
 * Within a layer, selection is the observation window and max-pool of SnapKV.
 *
 * **On a single-layer workload this policy is exactly SnapKV.** That is not a
 * hedge, it is arithmetic: with `numLayers: 1` there is nothing to redistribute
 * and the allocation returns `budget` unchanged. The registry's trace is
 * single-layer, so the benchmark table cannot distinguish the two and their
 * rows are identical by construction. The allocation rule is real and is
 * covered by its own vectors and tests at two and three layers; what is missing
 * is a multi-layer *workload* to run it against, and the README says so rather
 * than letting the table imply the policies were compared and tied.
 */

import type { KvCachePolicy } from "../../../packages/core/src/domains/kv-cache/interface.ts";

export interface PyramidKvParams {
  /** Average token positions kept per layer, before redistribution. */
  budget: number;
  /** Which layer this instance serves, counting from the input. */
  layer: number;
  /** Layers the budget is shared across. One means no redistribution. */
  numLayers: number;
  /** How many times more cache the first layer gets than the last. */
  pyramidRatio: number;
  /** The most recent positions, never evicted whatever their score. */
  recentWindow: number;
  /** How many recent decode steps the score is summed over. */
  obsWindow: number;
  /** Width of the max-pool across neighbouring kept positions. Must be odd. */
  poolKernel: number;
}

const DEFAULT_BUDGET = 512;
const DEFAULT_LAYER = 0;
/** One layer, so the pyramid is inert unless a caller asks for it. */
const DEFAULT_NUM_LAYERS = 1;
/**
 * Four, meaning the first layer gets four times the cache of the last.
 *
 * The paper's figures show the useful ratio varying widely by model, so this is
 * a starting point rather than a recommendation — and, being untestable on a
 * single-layer trace, it is not a number this registry has measured.
 */
const DEFAULT_PYRAMID_RATIO = 4;
const DEFAULT_RECENT_WINDOW = 32;
const DEFAULT_OBS_WINDOW = 16;
const DEFAULT_POOL_KERNEL = 7;

/**
 * How much cache layer `layer` gets when `numLayers` share `budget` on average.
 *
 * An arithmetic sequence from `2·budget·r/(r+1)` down to `2·budget/(r+1)`,
 * whose mean is `budget` by construction, evaluated as a single integer
 * division so that all three implementations floor at the same point:
 *
 *     effective(k) = 2·budget·( r·(L−1) − k·(r−1) ) / ( (r+1)·(L−1) )
 *
 * Exported because the allocation is the policy's actual contribution, and a
 * caller sizing a real multi-layer cache needs it before constructing anything.
 */
export function pyramidBudget(
  budget: number,
  layer: number,
  numLayers: number,
  pyramidRatio: number,
): number {
  // One layer has nothing to redistribute, and the formula's denominator would
  // be zero.
  if (numLayers <= 1) return budget;

  const span = numLayers - 1;
  const numerator = 2 * budget * (pyramidRatio * span - layer * (pyramidRatio - 1));
  const denominator = (pyramidRatio + 1) * span;
  return Math.floor(numerator / denominator);
}

export default class PyramidKv implements KvCachePolicy {
  /** This layer's share of the budget, after redistribution. */
  private readonly effective: number;
  private readonly recentWindow: number;
  private readonly obsWindow: number;
  private readonly poolRadius: number;
  private readonly capacity: number;
  private readonly positions: Int32Array;
  private readonly history: Float32Array;
  private readonly sums: Float64Array;
  private readonly pooled: Float64Array;
  private readonly doomed: Uint8Array;
  private readonly victims: number[] = [];
  private size = 0;
  private slot = 0;

  constructor(params: Partial<PyramidKvParams> = {}) {
    const budget = params.budget ?? DEFAULT_BUDGET;
    const layer = params.layer ?? DEFAULT_LAYER;
    const numLayers = params.numLayers ?? DEFAULT_NUM_LAYERS;
    const pyramidRatio = params.pyramidRatio ?? DEFAULT_PYRAMID_RATIO;
    const recentWindow = params.recentWindow ?? DEFAULT_RECENT_WINDOW;
    const obsWindow = params.obsWindow ?? DEFAULT_OBS_WINDOW;
    const poolKernel = params.poolKernel ?? DEFAULT_POOL_KERNEL;

    if (!Number.isInteger(budget) || budget < 1) {
      throw new RangeError(`PyramidKv: budget must be a positive integer, received ${budget}`);
    }
    if (!Number.isInteger(numLayers) || numLayers < 1) {
      throw new RangeError(
        `PyramidKv: numLayers must be a positive integer, received ${numLayers}`,
      );
    }
    if (!Number.isInteger(layer) || layer < 0 || layer >= numLayers) {
      throw new RangeError(
        `PyramidKv: layer must be an integer in [0, ${numLayers}), received ${layer}`,
      );
    }
    // A ratio below one would invert the pyramid, which is a different policy
    // and not this one; exactly one is the degenerate uniform case.
    if (!Number.isInteger(pyramidRatio) || pyramidRatio < 1) {
      throw new RangeError(
        `PyramidKv: pyramidRatio must be an integer of at least 1, received ${pyramidRatio}`,
      );
    }
    if (!Number.isInteger(recentWindow) || recentWindow < 0) {
      throw new RangeError(
        `PyramidKv: recentWindow must be a non-negative integer, received ${recentWindow}`,
      );
    }
    if (recentWindow >= budget) {
      throw new RangeError(
        `PyramidKv: recentWindow (${recentWindow}) must be smaller than budget ` +
          `(${budget}), or there is nothing the score is allowed to choose between.`,
      );
    }
    if (!Number.isInteger(obsWindow) || obsWindow < 1) {
      throw new RangeError(
        `PyramidKv: obsWindow must be a positive integer, received ${obsWindow}`,
      );
    }
    if (!Number.isInteger(poolKernel) || poolKernel < 1 || poolKernel % 2 === 0) {
      throw new RangeError(
        `PyramidKv: poolKernel must be a positive odd integer, received ${poolKernel}`,
      );
    }

    // A deep layer's share can fall below the recent window, at which point the
    // cache would be smaller than its own protected region — a state the
    // selection rule cannot express. It keeps the window plus one instead, so
    // there is always exactly one position the score is choosing about.
    const allocated = pyramidBudget(budget, layer, numLayers, pyramidRatio);
    this.effective = allocated < recentWindow + 1 ? recentWindow + 1 : allocated;

    this.recentWindow = recentWindow;
    this.obsWindow = obsWindow;
    this.poolRadius = (poolKernel - 1) / 2;
    // Room for whichever cap is larger: a shallow layer's share exceeds the
    // average, and a caller may still drive this at the average budget.
    this.capacity = (this.effective > budget ? this.effective : budget) + 1;

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

  /** Record this step's attention into the ring. See snapkv for the mechanism. */
  onDecodeStep(pos: number, attn: Float32Array | null): void {
    if (attn !== null) {
      const shared = Math.min(attn.length, this.size);
      for (let i = 0; i < shared; i += 1) {
        this.history[i * this.obsWindow + this.slot] = attn[i]!;
      }
      // A null vector is inert, ring included: the window spans the last
      // `obsWindow` observed steps, not the last `obsWindow` calls.
      this.slot += 1;
      if (this.slot === this.obsWindow) this.slot = 0;
    }

    if (this.size === this.capacity) {
      throw new RangeError(
        `PyramidKv: asked to hold ${this.size + 1} positions with room for ` +
          `${this.capacity - 1}. The caller's budget must match the policy's.`,
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
   * The target is the tighter of the caller's budget and this layer's share, so
   * a deep layer holds less than it was offered — which is the whole point —
   * while never exceeding what the caller asked for.
   */
  evict(budget: number): number[] {
    this.victims.length = 0;
    const target = budget < this.effective ? budget : this.effective;
    const needed = this.size - target;
    if (needed <= 0) return this.victims;

    for (let i = 0; i < this.size; i += 1) {
      const base = i * this.obsWindow;
      let total = 0;
      for (let s = 0; s < this.obsWindow; s += 1) total += this.history[base + s]!;
      this.sums[i] = total;
    }

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
        if (best === -1 || this.pooled[i]! < this.pooled[best]!) best = i;
      }
      if (best === -1) break;
      this.doomed[best] = 1;
    }

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
   * This layer's share of the budget after redistribution.
   *
   * Reporting only, and the number the whole policy turns on — a vector pins it
   * directly rather than leaving the allocation to be inferred from an
   * eviction three steps later.
   */
  effectiveBudget(): number {
    return this.effective;
  }

  /** The attention `pos` received over the observation window, unpooled. */
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
