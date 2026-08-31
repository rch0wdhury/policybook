/**
 * Scissorhands — count how many steps a token mattered for, not how much.
 *
 * Liu et al. proposed the **persistence of importance** hypothesis: a token
 * that was influential at one decoding step tends to keep being influential at
 * later ones, and a token that was not, stays uninfluential. If that holds, the
 * useful question is not *how much* attention a position has collected but *how
 * reliably* it attracts any.
 *
 * So a position earns a vote on every step where its attention exceeds its fair
 * share — `1 / kept`, what it would receive if attention were spread evenly —
 * and the fewest votes is what gets evicted.
 *
 * The difference from [h2o](../h2o/) is the difference between a sum and a
 * count, and it decides exactly one thing: what happens to a position that was
 * enormously important once and irrelevant since. H2O's cumulative score never
 * forgets, so that position defends its slot forever. Here it has a single vote
 * and loses to anything that has quietly cleared its share a few times. The
 * distinguishing vectors in both policies run identical steps and split
 * precisely there.
 *
 * Which is right depends on the workload, and neither is universally better —
 * a genuine one-off fact in a long document *should* be defended, and a stale
 * early spike should not, and nothing in either policy can tell those apart.
 */

import type { KvCachePolicy } from "../../../packages/core/src/domains/kv-cache/interface.ts";

export interface ScissorhandsParams {
  /** Maximum token positions kept in the cache. */
  budget: number;
  /** The most recent positions, never evicted whatever their vote count. */
  recentWindow: number;
}

const DEFAULT_BUDGET = 512;
/**
 * Thirty-two, matching h2o so the two are comparable.
 *
 * As there, the exact number matters less than that it is not zero: a token
 * generated this step has had no opportunity to vote at all.
 */
const DEFAULT_RECENT_WINDOW = 32;

export default class Scissorhands implements KvCachePolicy {
  private readonly recentWindow: number;
  /** `budget + 1`: the most that can be held before an eviction is asked for. */
  private readonly capacity: number;
  /** Kept positions in ascending order — the order `attn` arrives in. */
  private readonly positions: Int32Array;
  /** Steps on which each kept position exceeded its fair share. */
  private readonly votes: Int32Array;
  /** Scratch for the eviction pass, so `evict` allocates nothing. */
  private readonly doomed: Uint8Array;
  private readonly victims: number[] = [];
  private size = 0;

  constructor(params: Partial<ScissorhandsParams> = {}) {
    const budget = params.budget ?? DEFAULT_BUDGET;
    const recentWindow = params.recentWindow ?? DEFAULT_RECENT_WINDOW;

    if (!Number.isInteger(budget) || budget < 1) {
      throw new RangeError(`Scissorhands: budget must be a positive integer, received ${budget}`);
    }
    if (!Number.isInteger(recentWindow) || recentWindow < 0) {
      throw new RangeError(
        `Scissorhands: recentWindow must be a non-negative integer, received ${recentWindow}`,
      );
    }
    if (recentWindow >= budget) {
      throw new RangeError(
        `Scissorhands: recentWindow (${recentWindow}) must be smaller than budget ` +
          `(${budget}), or there is nothing the votes are allowed to choose between.`,
      );
    }

    this.recentWindow = recentWindow;
    this.capacity = budget + 1;
    this.positions = new Int32Array(this.capacity);
    this.votes = new Int32Array(this.capacity);
    this.doomed = new Uint8Array(this.capacity);

    // Position 0's token exists before the first decode step (see the domain
    // interface), and has voted on nothing yet.
    this.positions[0] = 0;
    this.votes[0] = 0;
    this.size = 1;
  }

  /**
   * Give a vote to every kept position that beat its fair share this step.
   *
   * The threshold is `1 / attn.length` — what each position would receive if
   * this step's attention were spread evenly over everything held. The
   * comparison is **strict**, so a position that exactly matches its share does
   * not vote; at the very first step, where one position holds all the
   * attention and its share is 1, that means no vote at all.
   */
  onDecodeStep(pos: number, attn: Float32Array | null): void {
    if (attn !== null && attn.length > 0) {
      // One float64 division per step, not per position.
      const share = 1 / attn.length;
      const shared = Math.min(attn.length, this.size);
      for (let i = 0; i < shared; i += 1) {
        if (attn[i]! > share) this.votes[i] = this.votes[i]! + 1;
      }
    }

    if (this.size === this.capacity) {
      throw new RangeError(
        `Scissorhands: asked to hold ${this.size + 1} positions with a budget of ` +
          `${this.capacity - 1}. The harness's budget must match the policy's.`,
      );
    }

    this.positions[this.size] = pos;
    this.votes[this.size] = 0;
    this.size += 1;
  }

  /**
   * Drop the least persistent positions outside the recent window.
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
    // goes per step, so this is a single linear scan.
    for (let taken = 0; taken < needed && taken < evictableEnd; taken += 1) {
      let best = -1;
      for (let i = 0; i < evictableEnd; i += 1) {
        if (this.doomed[i] === 1) continue;
        // Strictly less, so a tie leaves the earlier index standing — which is
        // the lower position, since the array is ascending. Ties are common
        // here in a way they are not for h2o: vote counts are small integers,
        // so the rule does real work rather than covering a rare coincidence.
        if (best === -1 || this.votes[i]! < this.votes[best]!) best = i;
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
      this.votes[write] = this.votes[read]!;
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
   * How many steps `pos` has beaten its fair share on, or -1 if not held.
   *
   * Reporting only — it exists so vectors can pin the voting rule directly,
   * including the strictness of the comparison, rather than inferring it from
   * which position was evicted.
   */
  votesOf(pos: number): number {
    for (let i = 0; i < this.size; i += 1) {
      if (this.positions[i] === pos) return this.votes[i]!;
    }
    return -1;
  }
}
