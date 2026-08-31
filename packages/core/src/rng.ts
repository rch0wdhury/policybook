/**
 * The deterministic random number generator shared by every Policybook policy,
 * trace generator and harness — in every language.
 *
 * Policies never reach for a global random source. Anything that needs
 * randomness receives an `Rng`, seeded from a single 32-bit number, so a run is
 * reproducible on any platform in any language (concept.md §9). This file is
 * the reference; `packages/python/policybook/rng.py` and
 * `packages/c/src/rng.c` reproduce it bit for bit, and
 * `rng.vectors.json` is the shared proof that they do.
 *
 * The generator is **xoshiro128\*\*** (Blackman and Vigna), seeded by
 * **splitmix32**. It is small, fast, has a 2^128 − 1 period, and passes the
 * usual statistical batteries. It is *not* cryptographically secure and must
 * never be used for anything security-sensitive.
 *
 * Every operation is explicit 32-bit unsigned arithmetic: `Math.imul` for
 * multiplication that wraps like C, `>>> 0` to bring a result back into
 * `[0, 2^32)`. Nothing here allocates after construction.
 */

/** Golden-ratio increment used by splitmix32. */
const SPLITMIX_GAMMA = 0x9e3779b9;
const SPLITMIX_MUL_1 = 0x21f0aaad;
const SPLITMIX_MUL_2 = 0x735a2d97;

/** 2^32, as a float64. Exact. */
const TWO_POW_32 = 4294967296;

/**
 * The splitmix32 finalising mix, applied to a value on its own.
 *
 * This is the canonical way to turn a key into a well-distributed 32-bit hash
 * anywhere in the registry — sketch indices, virtual-node placement, and the
 * like. It is deliberately the same mix used to expand a seed, so every
 * language already has the code.
 *
 * @param value any number; only its low 32 bits are used.
 * @returns a uint32 in `[0, 2^32)`.
 */
export function mix32(value: number): number {
  let z = value >>> 0;
  z = (z ^ (z >>> 16)) >>> 0;
  z = Math.imul(z, SPLITMIX_MUL_1) >>> 0;
  z = (z ^ (z >>> 15)) >>> 0;
  z = Math.imul(z, SPLITMIX_MUL_2) >>> 0;
  z = (z ^ (z >>> 15)) >>> 0;
  return z;
}

/**
 * A seeded, deterministic 32-bit random number generator.
 *
 * ```ts
 * const rng = new Rng(42);
 * rng.nextU32();     // 32 random bits
 * rng.nextFloat();   // [0, 1)
 * rng.nextInt(6);    // 0..5, no modulo bias
 * ```
 *
 * Two generators built with the same seed produce the same sequence, forever,
 * on every platform.
 */
export class Rng {
  // The state is four separate fields rather than an array: it keeps the hot
  // path free of bounds checks and of the `number | undefined` that indexed
  // access would otherwise introduce.
  private s0: number;
  private s1: number;
  private s2: number;
  private s3: number;

  /**
   * @param seed any number; only its low 32 bits are used. Every seed,
   *   including 0, produces a valid stream.
   */
  constructor(seed: number) {
    // splitmix32 expands the single seed word into the four state words.
    let state = seed >>> 0;
    const nextStateWord = (): number => {
      state = (state + SPLITMIX_GAMMA) >>> 0;
      let z = state;
      z = (z ^ (z >>> 16)) >>> 0;
      z = Math.imul(z, SPLITMIX_MUL_1) >>> 0;
      z = (z ^ (z >>> 15)) >>> 0;
      z = Math.imul(z, SPLITMIX_MUL_2) >>> 0;
      z = (z ^ (z >>> 15)) >>> 0;
      return z;
    };

    this.s0 = nextStateWord();
    this.s1 = nextStateWord();
    this.s2 = nextStateWord();
    this.s3 = nextStateWord();

    // xoshiro cannot start from all zeroes; it would emit zeroes forever.
    if ((this.s0 | this.s1 | this.s2 | this.s3) === 0) this.s0 = 1;
  }

  /**
   * The next 32 random bits.
   *
   * @returns a uint32 in `[0, 2^32)`.
   */
  nextU32(): number {
    const s1 = this.s1;

    // The "**" scrambler: rotl(s1 * 5, 7) * 9.
    let result = Math.imul(s1, 5) >>> 0;
    result = ((result << 7) | (result >>> 25)) >>> 0;
    result = Math.imul(result, 9) >>> 0;

    const t = (s1 << 9) >>> 0;
    const s2 = (this.s2 ^ this.s0) >>> 0;
    const s3 = (this.s3 ^ s1) >>> 0;
    this.s1 = (s1 ^ s2) >>> 0;
    this.s0 = (this.s0 ^ s3) >>> 0;
    this.s2 = (s2 ^ t) >>> 0;
    this.s3 = ((s3 << 11) | (s3 >>> 21)) >>> 0;

    return result;
  }

  /**
   * A float in `[0, 1)` with 32 bits of resolution.
   *
   * Deliberately *not* the 53-bit variant: one `nextU32` divided by 2^32 is the
   * same in TypeScript, Python and C with no rounding argument to have
   * (concept.md §9).
   */
  nextFloat(): number {
    return this.nextU32() / TWO_POW_32;
  }

  /**
   * A uniform integer in `[0, bound)`, with no modulo bias.
   *
   * Rejection sampling: values below the largest multiple of `bound` that fits
   * in 32 bits are accepted, the rest redrawn. The expected number of draws is
   * under 2 for every bound.
   *
   * @param bound exclusive upper limit, an integer in `[1, 2^32]`.
   */
  nextInt(bound: number): number {
    if (!Number.isInteger(bound) || bound < 1 || bound > TWO_POW_32) {
      throw new RangeError(
        `Rng.nextInt: bound must be an integer in [1, 2^32], received ${bound}`,
      );
    }
    // Discard the short tail so the remaining range is an exact multiple of bound.
    const threshold = (TWO_POW_32 - bound) % bound;
    let value = this.nextU32();
    while (value < threshold) value = this.nextU32();
    return value % bound;
  }
}
