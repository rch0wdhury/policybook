/**
 * Zipf sampling for the canonical traces.
 *
 * Real cache workloads are skewed: a few keys take most of the traffic. Zipf is
 * the standard model for that, and it is what the cache and rate-limiter traces
 * are built on (concept.md §10).
 *
 * **Why only two exponents.** A Zipf weight is `1 / rank^alpha`, which wants
 * `pow`. But `pow` is not correctly rounded — the same call can return
 * different doubles on different C standard libraries — and a trace that
 * differs by one ULP eventually samples a different key, which would break the
 * bit-identical traces the registry depends on.
 * So the two exponents in use are the two that need only `sqrt`, which IEEE-754
 * *does* require to be correctly rounded everywhere:
 *
 *   alpha = 1.00 → 1 / r
 *   alpha = 0.75 → 1 / (sqrt(r) * sqrt(sqrt(r)))     since r^0.75 = r^(1/2) * r^(1/4)
 *
 * This is why the concept's `zipf-0.8` trace ships as `zipf-0.75`.
 */

import type { Rng } from "./rng";

/** The exponents the registry supports. Both are computable with sqrt alone. */
export type ZipfAlpha = 1 | 0.75;

/**
 * The weight of one rank, without calling `pow`.
 *
 * @param rank zero-based rank; rank 0 is the most popular key.
 */
export function zipfWeight(rank: number, alpha: ZipfAlpha): number {
  const r = rank + 1;
  if (alpha === 1) return 1 / r;
  // r^0.75 = sqrt(r) * sqrt(sqrt(r)) — two correctly rounded operations.
  const s = Math.sqrt(r);
  const q = Math.sqrt(s);
  return 1 / (s * q);
}

/**
 * A precomputed Zipf distribution over ranks `0 .. size - 1`.
 *
 * The sampled rank *is* the key, so key 0 is the most popular. Sampling is
 * inverse-CDF by binary search: O(log size) per draw, one `nextFloat` consumed,
 * and no allocation.
 */
export class ZipfSampler {
  /** Ascending cumulative weights. `cumulative[size - 1]` is the total. */
  private readonly cumulative: Float64Array;
  private readonly total: number;

  readonly size: number;
  readonly alpha: ZipfAlpha;

  constructor(size: number, alpha: ZipfAlpha) {
    if (!Number.isInteger(size) || size < 1) {
      throw new RangeError(`ZipfSampler: size must be a positive integer, received ${size}`);
    }

    this.size = size;
    this.alpha = alpha;
    this.cumulative = new Float64Array(size);

    // Summed in ascending rank order, which fixes the floating-point result.
    // Any other order would give a slightly different total.
    let running = 0;
    for (let rank = 0; rank < size; rank += 1) {
      running += zipfWeight(rank, alpha);
      this.cumulative[rank] = running;
    }
    this.total = running;
  }

  /**
   * Draw a rank, consuming exactly one `nextFloat()`.
   *
   * Finds the first index whose cumulative weight exceeds `u`.
   */
  sample(rng: Rng): number {
    const target = rng.nextFloat() * this.total;
    const cumulative = this.cumulative;

    let low = 0;
    let high = this.size - 1;
    while (low < high) {
      const mid = (low + high) >> 1;
      // Bounds are guaranteed by the loop invariant; the assertion avoids the
      // undefined-check that noUncheckedIndexedAccess would otherwise force
      // into the inner loop of every trace generator.
      if (cumulative[mid]! > target) {
        high = mid;
      } else {
        low = mid + 1;
      }
    }
    return low;
  }
}
