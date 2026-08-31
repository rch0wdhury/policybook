/**
 * What separates the three backoff policies from each other.
 *
 * Each policy's own suite checks that policy. This one checks the claims the
 * READMEs make *about the comparison*, on the canonical workload — because the
 * domain's central assertion, that jitter is worth having, is not visible in
 * any single policy's behaviour. It is a fact about a fleet.
 */

import { describe, expect, it } from "vitest";
import {
  RETRY_TRACES,
  retryMetrics,
  runRetryEpisodes,
} from "../../packages/core/src/domains/retry";
import type { RetryMetrics, RetryPolicyFactory } from "../../packages/core/src/domains/retry";
import { Rng } from "../../packages/core/src/rng";
import Constant from "./constant/index";
import DecorrelatedJitter from "./decorrelated-jitter/index";
import EqualJitter from "./equal-jitter/index";
import Exponential from "./exponential/index";
import ExponentialFullJitter from "./exponential-full-jitter/index";
import RetryAfterAware from "./retry-after-aware/index";

const SPEC = RETRY_TRACES["outage-30s"]!;

const FACTORIES: Record<string, RetryPolicyFactory> = {
  constant: () => new Constant(),
  exponential: () => new Exponential(),
  "exponential-full-jitter": (rng) => new ExponentialFullJitter({}, rng),
  "equal-jitter": (rng) => new EqualJitter({}, rng),
  "decorrelated-jitter": (rng) => new DecorrelatedJitter({}, rng),
  "retry-after-aware": (rng) => new RetryAfterAware({}, rng),
};

function measure(name: string): RetryMetrics {
  return retryMetrics(runRetryEpisodes(FACTORIES[name]!, SPEC));
}

describe("the shared backoff ceiling", () => {
  it("is computed identically by the jittered and un-jittered policies", () => {
    // `exponential-full-jitter` restates the ceiling rather than importing it
    // from `exponential`, because `policybook add` copies a policy file whole
    // and a copy that reached back into a sibling policy would not compile in
    // the reader's project. That trade buys self-containment at the cost of a
    // duplicate, so the duplicate is pinned here: the un-jittered policy's
    // delay is exactly the ceiling, and a jittered draw can never exceed it.
    const params = { baseMs: 100, capMs: 10_000, maxAttempts: 40 };
    const plain = new Exponential(params);

    for (let attempt = 1; attempt <= 30; attempt += 1) {
      const ceiling = plain.nextDelay(attempt, { status: 503, retryable: true });
      expect(ceiling).not.toBeNull();

      // Draw many times at this attempt number: every one must fit under the
      // un-jittered policy's answer, and over enough draws the maximum reaches
      // it. If the two copies had drifted, one of these would fail.
      const jitter = new ExponentialFullJitter(params, new Rng(attempt));
      let largest = 0;
      for (let draw = 0; draw < 400; draw += 1) {
        const delay = jitter.nextDelay(attempt, { status: 503, retryable: true })!;
        expect(delay).toBeLessThanOrEqual(ceiling!);
        if (delay > largest) largest = delay;
      }
      // With 400 draws the observed maximum sits close to the ceiling; a
      // drifted copy computing a different ceiling would miss by a factor of
      // two, not a few percent.
      expect(largest).toBeGreaterThan(ceiling! * 0.9);
    }
  });
});

describe("on the canonical workload", () => {
  const constant = measure("constant");
  const exponential = measure("exponential");
  const fullJitter = measure("exponential-full-jitter");

  it("shows constant backoff barely surviving a 30-second outage", () => {
    // Seven hundred milliseconds of patience against outages averaging fifteen
    // seconds. The number looks broken and is not: it is what a `sleep(100)`
    // loop actually achieves.
    expect(constant.successRate).toBeLessThan(0.05);
    expect(exponential.successRate).toBeGreaterThan(constant.successRate * 10);
  });

  it("shows full jitter succeeding less often than plain exponential", () => {
    // Stated plainly because it is the uncomfortable half of the result, and a
    // reader who found it themselves would rightly distrust a page that had
    // hidden it. A uniform draw from [0, ceiling] averages half the ceiling, so
    // full jitter waits half as long and reaches the recovery less often
    // within the same attempt budget.
    expect(fullJitter.successRate).toBeLessThan(exponential.successRate);
    expect(fullJitter.meanTimeToSuccessMs).toBeLessThan(exponential.meanTimeToSuccessMs);
  });

  it("shows full jitter cutting the peak simultaneous retries several-fold", () => {
    // And this is why it is the recommended default anyway. The un-jittered
    // policies put every client's nth retry in one 10 ms window; full jitter
    // spreads them, and the recovering service feels the difference.
    expect(exponential.peakRetryShare).toBeGreaterThan(0.1);
    expect(constant.peakRetryShare).toBeGreaterThan(0.1);
    expect(fullJitter.peakRetryShare).toBeLessThan(exponential.peakRetryShare / 4);
  });

  it("shows the un-jittered policies costing the same peak despite different curves", () => {
    // Backing off exponentially converts a continuous herd into a periodic one;
    // it does not disperse it. Constant and exponential differ enormously in
    // when they retry and hardly at all in how synchronised they are.
    expect(Math.abs(constant.peakRetryShare - exponential.peakRetryShare)).toBeLessThan(0.05);
  });

  it("costs every guessing policy about the same number of attempts", () => {
    // The attempt budget binds for all of them, so `meanAttempts` cannot rank
    // them — which is exactly why the domain reports the peak as well. The
    // aware policy is the exception, because it is told the answer.
    for (const metrics of [constant, exponential, fullJitter]) {
      expect(metrics.meanAttempts).toBeGreaterThan(7);
      expect(metrics.p99Attempts).toBe(8);
    }
  });
});

describe("the jitter family, ordered by how much they randomise", () => {
  const exponential = measure("exponential");
  const equal = measure("equal-jitter");
  const fullJitter = measure("exponential-full-jitter");
  const decorrelated = measure("decorrelated-jitter");

  it("puts equal jitter between exponential and full jitter on success", () => {
    // The arithmetic predicts it: expected delays of one, three quarters and
    // one half of the ceiling, so success rates in that order within the same
    // attempt budget. Seeing the three land in the predicted order is a check
    // on the harness as much as on the policies.
    expect(equal.successRate).toBeLessThan(exponential.successRate);
    expect(equal.successRate).toBeGreaterThan(fullJitter.successRate);
  });

  it("puts equal jitter between them on the herd too", () => {
    expect(equal.peakRetryShare).toBeLessThan(exponential.peakRetryShare / 4);
    expect(equal.peakRetryShare).toBeGreaterThan(fullJitter.peakRetryShare);
  });

  it("gives decorrelated jitter the lowest peak of any policy", () => {
    // Whole schedules diverge rather than individual attempts, which spreads a
    // fleet more thoroughly than any per-call draw.
    for (const other of [exponential, equal, fullJitter]) {
      expect(decorrelated.peakRetryShare).toBeLessThan(other.peakRetryShare);
    }
  });

  it("makes decorrelated jitter the slowest to succeed, for the same reason", () => {
    // It climbs by about 1.5x a step against exponential's 2x, so within the
    // same attempt budget it covers less elapsed time per success.
    expect(decorrelated.meanTimeToSuccessMs).toBeGreaterThan(fullJitter.meanTimeToSuccessMs);
    expect(decorrelated.meanTimeToSuccessMs).toBeGreaterThan(equal.meanTimeToSuccessMs);
  });
});

describe("reading the server's answer", () => {
  const aware = measure("retry-after-aware");
  const fullJitter = measure("exponential-full-jitter");

  it("succeeds far more often than any policy that guesses", () => {
    // It is told how long the outage will last. Nothing that infers can compete.
    expect(aware.successRate).toBeGreaterThan(0.95);
    expect(aware.successRate).toBeGreaterThan(fullJitter.successRate * 4);
  });

  it("costs far fewer attempts", () => {
    expect(aware.meanAttempts).toBeLessThan(fullJitter.meanAttempts / 1.5);
  });

  it("and re-synchronises the fleet worse than doing nothing at all", () => {
    // The honest cost, asserted rather than only described. 835 of the 1,000
    // episodes have an outage longer than the harness's five-second cap on what
    // a server will ask for, so all 835 receive the identical hint and return
    // together. The clamp itself creates the herd, and real services clamp the
    // same way.
    expect(aware.peakRetryShare).toBeGreaterThan(measure("constant").peakRetryShare);
    expect(aware.peakRetryShare).toBeGreaterThan(fullJitter.peakRetryShare * 5);
  });
});
