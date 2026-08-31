/**
 * The `retry` domain: how long to wait before trying again.
 *
 * The smallest decision in the registry and the one most often got wrong. A
 * request failed; something downstream is unhappy; when should the client come
 * back? Retry too eagerly and a service that was merely slow becomes a service
 * that is down, because every client in the fleet is now hammering it in
 * lockstep. Retry too lazily and you have turned a two-second blip into a
 * thirty-second outage for your users.
 *
 * The interesting part is not the delay curve — everyone knows to back off
 * exponentially — but the **randomness**. Without jitter, every client that saw
 * the same failure retries at the same instant, and the recovering service is
 * hit by a synchronised wave rather than a spread. That is the thundering herd,
 * and it is why the policies in this domain differ mostly in where they put the
 * random draw (concept.md §5.1).
 *
 * A policy is handed an `Rng` at construction rather than owning one. That is
 * what makes a retry sequence reproducible in a test and a simulation, and it
 * is the same rule as everywhere else here: nothing reads a clock or a global.
 *
 * **Deviation from concept.md §5.1**, which threads the `Rng` through
 * `nextDelay` as a third argument. Every other domain in this registry supplies
 * randomness at construction — the C vtable's `create(params, allocator, rng)`
 * already does, and so do the cache and rate-limiter policies — and making
 * retry the one domain that passes it per call would leave the C interface
 * carrying an `pb_rng *` on its hot path for no benefit. The property that
 * matters, that a policy never reaches for a global source, is unchanged.
 */

/** What went wrong, as much of it as a policy is allowed to know. */
export interface RetryError {
  /** HTTP-style status, when there is one. */
  status?: number;
  /** Whether retrying could plausibly help at all. */
  retryable: boolean;
  /**
   * How long the server asked the client to wait, in milliseconds.
   *
   * A harness extension rather than part of the original interface, and
   * optional: most errors do not carry one. `Retry-After` is the server's own
   * estimate of when it will be ready, and a policy that ignores it is
   * guessing when it has been told. T27's `retry-after-aware` policy is the one
   * that reads it.
   */
  retryAfterMs?: number;
}

/** Every retry policy implements this, and nothing more. */
export interface RetryPolicy {
  /**
   * How long to wait before attempt `attempt + 1`, or null to give up.
   *
   * `attempt` is 1-based: it is the number of the attempt that just failed, so
   * the first call always has `attempt === 1`. Returning null is a decision, not
   * an error — it means this policy believes further attempts are not worth
   * making, and the caller should surface the failure.
   */
  nextDelay(attempt: number, error: RetryError): number | null;
}

/** Every retry policy takes at least these. */
export interface RetryParams {
  /** The unit of delay, in milliseconds. */
  baseMs: number;
  /** No single delay exceeds this, however many attempts have failed. */
  capMs: number;
  /** Give up after this many attempts. */
  maxAttempts: number;
}

/** The reference configuration every canonical benchmark uses. */
export const RETRY_REFERENCE: RetryParams = {
  baseMs: 100,
  capMs: 10_000,
  maxAttempts: 8,
};
