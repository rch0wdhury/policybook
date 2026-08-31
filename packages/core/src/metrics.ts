/**
 * Shared metric helpers.
 *
 * Benchmark numbers are committed and compared across languages, so rounding
 * has to be specified rather than assumed.
 */

/**
 * Round to six decimal places, half away from zero.
 *
 * Six places is enough to separate policies on a million-event trace without
 * recording noise. The rounding mode matters for ports: Python's built-in
 * `round` and C's `rint` are half-to-even and would disagree on an exact tie,
 * so all three languages spell it as `floor(value * 1e6 + 0.5) / 1e6`. Not
 * `Math.round`: for exactly one double (0.49999999999999994) the addition
 * rounds up to 1.0 where `Math.round` answers 0, so the two are *almost*
 * interchangeable — and "almost" is the wrong word to have inside a
 * bit-exactness contract. The reference implements the portable spelling.
 *
 * All registry metrics are non-negative, so there is no negative-tie case to
 * argue about.
 */
export function round6(value: number): number {
  if (!Number.isFinite(value)) return value;
  return Math.floor(value * 1e6 + 0.5) / 1e6;
}
