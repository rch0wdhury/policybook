/**
 * TOVA — drop whichever token the model just stopped looking at.
 *
 * Oren et al. argued that a decoder-only transformer is really a multi-state
 * RNN whose state is the KV cache, and that a fixed-size cache turns it into an
 * ordinary RNN. From that angle the eviction rule is obvious: at each step the
 * model tells you, through its attention, which states it is currently using.
 * Drop the least-used one.
 *
 * The whole policy is one line — evict the position with the lowest attention
 * **on the current step** — and what makes it distinctive is what it leaves
 * out. There is no accumulation, so nothing a position did earlier defends it.
 * [h2o](../h2o/) sums attention over all time and will protect a position that
 * mattered a thousand steps ago; this policy has no memory of that at all.
 *
 * That makes it the most responsive policy here and the most forgetful, and
 * which of those you get depends entirely on the workload. When importance
 * genuinely shifts — a long document where the model moves from section to
 * section — tracking the current step is right and accumulating is wrong. When
 * importance is stable, a single quiet step is enough to lose a token the model
 * will want back.
 *
 * **There is no recent-window protection**, deliberately, and unlike every
 * other scoring policy in this domain. The paper does not have one, and the
 * reason it does not need one is that recency is already in the signal: recent
 * tokens attract high attention *now*, so the current step's weights protect
 * them without a rule. The one position this cannot cover is the token just
 * generated, which has not yet been attended to by anything — see
 * `onDecodeStep`.
 */

import type { KvCachePolicy } from "../../../packages/core/src/domains/kv-cache/interface.ts";

export interface TovaParams {
  /** Maximum token positions kept in the cache. */
  budget: number;
}

const DEFAULT_BUDGET = 512;

/**
 * Marks a position that has never appeared in an attention vector.
 *
 * Attention weights are non-negative, so a negative value is unambiguous.
 */
const UNOBSERVED = -1;

export default class Tova implements KvCachePolicy {
  /** `budget + 1`: the most that can be held before an eviction is asked for. */
  private readonly capacity: number;
  /** Kept positions in ascending order — the order `attn` arrives in. */
  private readonly positions: Int32Array;
  /** The most recent attention each position received, or UNOBSERVED. */
  private readonly lastAttn: Float64Array;
  /** Scratch for the eviction pass, so `evict` allocates nothing. */
  private readonly doomed: Uint8Array;
  private readonly victims: number[] = [];
  private size = 0;

  constructor(params: Partial<TovaParams> = {}) {
    const budget = params.budget ?? DEFAULT_BUDGET;

    if (!Number.isInteger(budget) || budget < 1) {
      throw new RangeError(`Tova: budget must be a positive integer, received ${budget}`);
    }

    this.capacity = budget + 1;
    this.positions = new Int32Array(this.capacity);
    this.lastAttn = new Float64Array(this.capacity);
    this.doomed = new Uint8Array(this.capacity);

    // Position 0's token exists before the first decode step (see the domain
    // interface) and nothing has attended to it yet.
    this.positions[0] = 0;
    this.lastAttn[0] = UNOBSERVED;
    this.size = 1;
  }

  /**
   * Record this step's attention, replacing whatever was there before.
   *
   * No accumulation: the previous value is discarded outright, which is the
   * entire policy.
   *
   * The token being generated is admitted as **unobserved**, because nothing
   * has attended to it yet — it did not appear in this step's attention vector,
   * which covers only the positions that existed before it. An unobserved
   * position is never evicted, so in practice the newest token is safe for
   * exactly one eviction, until the next step gives it a real weight. That is
   * not a recency rule smuggled back in; it is a refusal to rank a position on
   * evidence that does not exist yet.
   */
  onDecodeStep(pos: number, attn: Float32Array | null): void {
    if (attn !== null) {
      const shared = Math.min(attn.length, this.size);
      for (let i = 0; i < shared; i += 1) {
        this.lastAttn[i] = attn[i]!;
      }
    }

    if (this.size === this.capacity) {
      throw new RangeError(
        `Tova: asked to hold ${this.size + 1} positions with a budget of ` +
          `${this.capacity - 1}. The harness's budget must match the policy's.`,
      );
    }

    this.positions[this.size] = pos;
    this.lastAttn[this.size] = UNOBSERVED;
    this.size += 1;
  }

  /**
   * Drop the positions the model attended to least on the latest step.
   *
   * The returned array is reused between calls, which the domain interface
   * permits: the harness consumes it before the policy is called again.
   */
  evict(budget: number): number[] {
    this.victims.length = 0;
    const needed = this.size - budget;
    if (needed <= 0) return this.victims;

    // Repeated argmin rather than a sort: in steady state exactly one position
    // goes per step, so this is a single linear scan.
    for (let taken = 0; taken < needed; taken += 1) {
      let best = -1;
      for (let i = 0; i < this.size; i += 1) {
        if (this.doomed[i] === 1) continue;
        // An unobserved position is not a candidate: it has no weight to be
        // ranked on, and treating its absence as zero would evict every token
        // the step it was generated.
        if (this.lastAttn[i] === UNOBSERVED) continue;
        // Strictly less, so a tie leaves the earlier index standing — which is
        // the lower position, since the array is ascending.
        if (best === -1 || this.lastAttn[i]! < this.lastAttn[best]!) best = i;
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
      this.lastAttn[write] = this.lastAttn[read]!;
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
   * The attention `pos` received on the latest step it was present for.
   *
   * Returns -1 when the position is not held, or is held but has never been
   * attended to. Reporting only — it exists so vectors can pin the replacement
   * rule directly rather than inferring it from which position was evicted.
   */
  lastAttentionOf(pos: number): number {
    for (let i = 0; i < this.size; i += 1) {
      if (this.positions[i] === pos) return this.lastAttn[i]!;
    }
    return UNOBSERVED;
  }
}
