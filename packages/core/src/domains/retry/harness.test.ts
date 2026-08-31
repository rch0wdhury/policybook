import { describe, expect, it } from "vitest";
import { Rng } from "../../rng";
import { HERD_WINDOW_MS, runRetryEpisodes } from "./harness";
import type { RetryError, RetryPolicy } from "./interface";
import { retryMetrics } from "./metrics";
import { RETRY_TRACES, environmentSeed, generateRetryTrace, policySeed } from "./traces";
import type { RetryTraceSpec } from "./traces";

const SPEC = RETRY_TRACES["outage-30s"]!;

/** Waits a fixed amount, forever — the simplest thing the harness can drive. */
class FixedDelay implements RetryPolicy {
  readonly seen: { attempt: number; retryAfterMs: number | undefined }[] = [];

  constructor(
    private readonly delayMs: number,
    private readonly maxAttempts = Number.POSITIVE_INFINITY,
  ) {}

  nextDelay(attempt: number, error: RetryError): number | null {
    this.seen.push({ attempt, retryAfterMs: error.retryAfterMs });
    if (attempt >= this.maxAttempts) return null;
    return this.delayMs;
  }
}

function tiny(overrides: Partial<RetryTraceSpec> = {}): RetryTraceSpec {
  return { ...SPEC, episodes: 4, ...overrides };
}

describe("runRetryEpisodes", () => {
  it("draws the outage the trace generator publishes", () => {
    // The committed trace is a projection of the harness's own environment
    // stream, so the two must agree by construction. If they ever did not, the
    // parity tests would be checking something the benchmark does not run.
    const outages = generateRetryTrace("outage-30s", 50);
    for (let episode = 0; episode < outages.length; episode += 1) {
      const rng = new Rng(environmentSeed(SPEC, episode));
      expect(rng.nextInt(SPEC.maxOutageMs)).toBe(outages[episode]);
    }
  });

  it("gives every policy the same outages and the same coin flips", () => {
    // The reason the environment and policy streams are separate. A policy that
    // draws jitter must not shift the environment's rolls, or two policies
    // would face different luck rather than different strategies.
    const quiet = runRetryEpisodes(() => new FixedDelay(1_000), tiny({ episodes: 200 }));
    const noisy = runRetryEpisodes(
      (rng) => ({
        nextDelay: () => {
          // Two draws per attempt, from the policy's own stream. If the
          // environment shared that stream, these would move every later roll.
          rng.nextInt(50);
          rng.nextInt(50);
          return 1_000;
        },
      }),
      tiny({ episodes: 200 }),
    );

    // Identical delays, so identical everything — despite one policy consuming
    // two extra draws per attempt.
    expect(noisy.successes).toBe(quiet.successes);
    expect(noisy.totalAttempts).toBe(quiet.totalAttempts);
    expect(noisy.totalTimeToSuccessMs).toBe(quiet.totalTimeToSuccessMs);
  });

  it("passes the server's own estimate down as retryAfterMs", () => {
    const policy = new FixedDelay(1_000, 3);
    runRetryEpisodes(() => policy, tiny({ episodes: 1 }));

    const outage = generateRetryTrace("outage-30s", 1)[0]!;
    // First attempt is at t=0, so the server asks for the whole outage —
    // clamped to the five seconds a server would actually ask for.
    expect(policy.seen[0]!.retryAfterMs).toBe(Math.min(outage, 5_000));
    // A second later it asks for a second less.
    expect(policy.seen[1]!.retryAfterMs).toBe(Math.min(Math.max(outage - 1_000, 0), 5_000));
  });

  it("counts an episode that gives up, and says why", () => {
    const result = runRetryEpisodes(() => new FixedDelay(1_000, 3), tiny({ episodes: 100 }));
    expect(result.gaveUp).toBeGreaterThan(0);
    expect(result.gaveUp + result.successes + result.deadlineExceeded).toBe(100);
  });

  it("abandons an episode whose next attempt would pass the deadline", () => {
    // A delay longer than the deadline can never produce a second attempt.
    const result = runRetryEpisodes(() => new FixedDelay(SPEC.deadlineMs + 1), tiny({ episodes: 50 }));
    expect(result.deadlineExceeded).toBeGreaterThan(0);
    expect(result.gaveUp).toBe(0);
  });

  it("rejects a delay that is not a non-negative integer", () => {
    for (const bad of [-1, 1.5, Number.NaN]) {
      const policy: RetryPolicy = { nextDelay: () => bad };
      expect(() => runRetryEpisodes(() => policy, tiny({ episodes: 1 }))).toThrow(
        /must be a non-negative integer/,
      );
    }
  });

  it("accepts a delay of zero, which is a real strategy", () => {
    // Retrying immediately is what full jitter does when it draws zero.
    const result = runRetryEpisodes(() => new FixedDelay(0, 5), tiny({ episodes: 10 }));
    expect(result.totalAttempts).toBe(50);
  });

  describe("the herd measure", () => {
    it("counts every client's nth retry together when there is no jitter", () => {
      // A fixed delay puts every surviving client at exactly 1,000 ms, then
      // 2,000, and so on, so the peak window holds one retry for each episode
      // that got as far as a second attempt — the exact herd.
      const result = runRetryEpisodes(() => new FixedDelay(1_000, 4), tiny({ episodes: 100 }));
      const reachedSecondAttempt = [...result.attemptsSorted].filter((n) => n >= 2).length;

      expect(reachedSecondAttempt).toBeGreaterThan(90);
      expect(result.peakRetriesInWindow).toBe(reachedSecondAttempt);
    });

    it("falls when the policy spreads its retries", () => {
      // The same delay on average, jittered over a range far wider than the
      // window. The counts of retries differ slightly — different delays mean
      // episodes end at different points — so the comparison is on the share,
      // which is what the metric reports.
      const jittered = retryMetrics(
        runRetryEpisodes(
          (rng) => ({
            nextDelay: (attempt) => (attempt >= 4 ? null : 500 + rng.nextInt(1_001)),
          }),
          tiny({ episodes: 100 }),
        ),
      );
      const lockstep = retryMetrics(
        runRetryEpisodes(() => new FixedDelay(1_000, 4), tiny({ episodes: 100 })),
      );

      expect(jittered.peakRetryShare).toBeLessThan(lockstep.peakRetryShare / 4);
    });

    it("is measured over a window smaller than the reference base delay", () => {
      // Stated as a test because the choice is load-bearing: at a window as
      // wide as the first delay, a jittered policy and a lockstep one are
      // indistinguishable.
      expect(HERD_WINDOW_MS).toBeLessThan(100);
    });
  });
});

describe("retryMetrics", () => {
  it("reports zeroes on an empty run rather than dividing by zero", () => {
    const result = runRetryEpisodes(() => new FixedDelay(100, 1), tiny({ episodes: 0 }));
    expect(retryMetrics(result)).toEqual({
      successRate: 0,
      meanTimeToSuccessMs: 0,
      meanAttempts: 0,
      p99Attempts: 0,
      peakRetryShare: 0,
    });
  });

  it("takes the 99th percentile by nearest rank, without interpolating", () => {
    // 100 episodes, all identical, so every percentile is that value — the
    // check that matters is that it returns an observed attempt count rather
    // than an average of two.
    const result = runRetryEpisodes(() => new FixedDelay(0, 3), tiny({ episodes: 100 }));
    expect(retryMetrics(result).p99Attempts).toBe(3);
    expect(Number.isInteger(retryMetrics(result).p99Attempts)).toBe(true);
  });

  it("averages time to success over the successes alone", () => {
    const result = runRetryEpisodes(() => new FixedDelay(1_000, 8), tiny({ episodes: 200 }));
    const metrics = retryMetrics(result);
    expect(result.successes).toBeGreaterThan(0);
    expect(metrics.meanTimeToSuccessMs).toBeCloseTo(
      result.totalTimeToSuccessMs / result.successes,
      4,
    );
  });
});

describe("the episode set", () => {
  it("names the traces it knows when given one it does not", () => {
    expect(() => generateRetryTrace("nope")).toThrow(/unknown retry trace "nope"/);
  });

  it("is reproducible, and truncation is a prefix", () => {
    const full = generateRetryTrace("outage-30s", 50);
    const short = generateRetryTrace("outage-30s", 10);
    expect(Array.from(short)).toEqual(Array.from(full).slice(0, 10));
  });

  it("keeps every outage inside its bound", () => {
    for (const outage of generateRetryTrace("outage-30s")) {
      expect(outage).toBeLessThan(SPEC.maxOutageMs);
    }
  });

  it("separates the environment and policy streams for every episode", () => {
    const seen = new Set<number>();
    for (let episode = 0; episode < SPEC.episodes; episode += 1) {
      seen.add(environmentSeed(SPEC, episode));
      seen.add(policySeed(SPEC, episode));
    }
    // No seed is ever reused, so no episode's policy shares a stream with any
    // episode's environment.
    expect(seen.size).toBe(SPEC.episodes * 2);
  });
});
