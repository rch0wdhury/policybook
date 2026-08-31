/**
 * The `rate-limiter` domain: deciding whether a request may proceed.
 *
 * A limiter is asked one question — may this request go through, right now? —
 * and the interesting part is what it does with the ones it refuses. A fixed
 * window is trivial to implement and lets through twice its limit at a window
 * boundary. A token bucket absorbs bursts by design. A leaky bucket refuses to.
 * Which of those is correct depends entirely on what is downstream, and that is
 * the choice this domain exists to make explicit.
 *
 * **Time is an integer number of milliseconds, supplied by the caller.** A
 * policy never reads the clock: it cannot be tested if it does, and a caller
 * with its own clock source (a simulation, a replay, a distributed counter)
 * would have no way to use it. Every arithmetic operation on that time is
 * integer arithmetic too — milli-token ledgers with an explicit carry rather
 * than floating-point token counts. Floats drift,
 * and three languages drift differently.
 *
 * **Keys are integers**, as everywhere else in the registry (§2.1). A caller
 * whose keys are strings hashes them; `mix32` is the registry's default and is
 * already shared by all three languages.
 */

/** Every rate-limiter policy implements this. */
export interface RateLimiterPolicy {
  /**
   * May a request of `cost` units for `key` proceed at time `now`?
   *
   * `now` is a non-decreasing integer in milliseconds. Returning true is the
   * decision *and* the commitment: whatever budget the request consumes has
   * been consumed by the time this returns, so a policy must not be asked
   * speculatively.
   */
  allow(key: number, cost: number, now: number): boolean;

  /**
   * How long until `key` could succeed, in milliseconds. A hint, not a promise.
   *
   * Zero means "try now". The value is what the policy can prove from its own
   * state at `now`; it never accounts for requests that have not happened yet,
   * so a caller that waits exactly this long may still be refused if others
   * arrive in the meantime.
   */
  retryAfter?(key: number, now: number): number;

  /**
   * How many keys the policy is currently tracking.
   *
   * Optional introspection, used for the memory metric. It is the number that separates a limiter you can run for a
   * million keys from one you cannot: a fixed window keeps a counter per key
   * forever unless it expires them, while a sliding log keeps every timestamp.
   */
  stateSize?(): number;
}

/**
 * The reference configuration every canonical benchmark uses.
 *
 * Policies express their limits differently — permits per window, tokens per
 * second, an emission interval — so this is stated once in neutral terms and
 * each policy's README says how it maps onto its own parameters. Without a
 * shared reference the benchmark table would compare policies configured
 * differently, which is worse than no table at all.
 */
export const RATE_LIMITER_REFERENCE = {
  /** Sustained rate, in permits per second. */
  permitsPerSecond: 100,
  /** How far above the sustained rate a policy may let a burst run. */
  burst: 100,
} as const;
