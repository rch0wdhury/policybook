/**
 * Full jitter — pick a delay uniformly at random from zero to the exponential
 * ceiling.
 *
 * **The default, and the one to ship unless you have a specific reason not
 * to.** It keeps everything [exponential](../exponential/) backoff gets right —
 * load on a struggling service falls geometrically as an outage continues — and
 * fixes the thing it gets wrong: clients no longer retry in lockstep, because
 * each one picks its own delay.
 *
 * The change is one line. Instead of waiting exactly `min(cap, base * 2^n)`,
 * wait a uniform random amount *between zero and that*. AWS's 2015 analysis,
 * which is where the name comes from, measured this against the alternatives on
 * a contended workload and found it both completed sooner and did less total
 * work than the un-jittered version — a rare case where the more random option
 * is better on every axis at once.
 *
 * The reason it wins is not obvious and is worth stating: the expected delay
 * *halves*, since a uniform draw from `[0, c]` averages `c/2`. Full jitter is
 * therefore more aggressive than plain exponential, not less. It comes out
 * ahead anyway because the spreading matters more than the average — a service
 * recovering from an outage cares far more about requests arriving evenly than
 * about their arriving slightly later.
 *
 * If halving the expected delay is too aggressive for your service,
 * [equal-jitter](../equal-jitter/) keeps half the delay fixed.
 */

import { Rng } from "../../../packages/core/src/rng.ts";
import type { RetryError } from "../../../packages/core/src/domains/retry/interface.ts";

/**
 * `min(cap, base * 2^(attempt - 1))`, in integers and without overflowing.
 *
 * The same function `retry/exponential` exports. It is restated here rather
 * than imported because a policy file is copied out of the registry whole by
 * `policybook add`, and a copy that reached back into a sibling policy would
 * not compile in the reader's project. `backoff-policies.test.ts` asserts the
 * two agree on every input, which is the anti-drift guarantee the import would
 * have given.
 *
 * Doubling stops as soon as the cap is reached, so the loop can never run past
 * the width of the integer no matter how large `attempt` is.
 */
function backoffCeiling(attempt: number, baseMs: number, capMs: number): number {
  let delay = baseMs;
  for (let step = 1; step < attempt; step += 1) {
    if (delay >= capMs) return capMs;
    delay *= 2;
  }
  return Math.min(delay, capMs);
}

export interface ExponentialFullJitterParams {
  /** The first ceiling, doubled on each subsequent attempt. */
  baseMs: number;
  /** No ceiling exceeds this, however many attempts have failed. */
  capMs: number;
  /** Give up after this many attempts. */
  maxAttempts: number;
}

const DEFAULT_BASE_MS = 100;
const DEFAULT_CAP_MS = 10_000;
const DEFAULT_MAX_ATTEMPTS = 8;

export default class ExponentialFullJitter {
  private readonly baseMs: number;
  private readonly capMs: number;
  private readonly maxAttempts: number;
  private readonly rng: Rng;

  /**
   * The `rng` is supplied by the caller, exactly as it is for every other
   * policy in the registry. A policy that reached for a global source could not
   * be replayed, and replaying a retry sequence is most of what testing one
   * consists of.
   */
  constructor(params: Partial<ExponentialFullJitterParams> = {}, rng?: Rng) {
    const baseMs = params.baseMs ?? DEFAULT_BASE_MS;
    const capMs = params.capMs ?? DEFAULT_CAP_MS;
    const maxAttempts = params.maxAttempts ?? DEFAULT_MAX_ATTEMPTS;

    if (!Number.isInteger(baseMs) || baseMs < 1) {
      throw new RangeError(
        `ExponentialFullJitter: baseMs must be a positive integer, received ${baseMs}`,
      );
    }
    if (!Number.isInteger(capMs) || capMs < 1) {
      throw new RangeError(
        `ExponentialFullJitter: capMs must be a positive integer, received ${capMs}`,
      );
    }
    if (!Number.isInteger(maxAttempts) || maxAttempts < 1) {
      throw new RangeError(
        `ExponentialFullJitter: maxAttempts must be a positive integer, received ${maxAttempts}`,
      );
    }

    this.baseMs = baseMs;
    this.capMs = capMs;
    this.maxAttempts = maxAttempts;
    // Seeded rather than left undefined: a policy constructed without one still
    // has to produce a delay, and an unseeded default would be a global source
    // by another name.
    this.rng = rng ?? new Rng(0);
  }

  nextDelay(attempt: number, error: RetryError): number | null {
    if (!error.retryable) return null;
    if (attempt >= this.maxAttempts) return null;

    // `nextInt(n)` returns 0..n-1, so the bound is the ceiling plus one and the
    // ceiling itself remains reachable. A delay of zero is reachable too, and
    // that is deliberate: some client retrying immediately is what makes the
    // arrival pattern smooth rather than merely delayed.
    const ceiling = backoffCeiling(attempt, this.baseMs, this.capMs);
    return this.rng.nextInt(ceiling + 1);
  }
}
