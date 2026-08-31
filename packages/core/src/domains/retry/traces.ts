/**
 * Canonical workloads for the retry domain.
 *
 * A retry "trace" is not a stream of events but a set of independent
 * **episodes**: something broke, and a client keeps trying until it succeeds or
 * gives up. What varies between episodes is how long the outage lasts, so the
 * trace is the sequence of outage durations — one per episode, drawn once and
 * reproduced identically by every port.
 *
 * The harness draws those same durations itself, from the same per-episode
 * seed; this file exists so the draw can be committed and compared across
 * languages without running a simulation (concept.md §10). If a port's Rng or
 * its seeding differed, these numbers would differ first.
 *
 * The full prose specification is in TRACES.md next to this file; the code and
 * that document must agree.
 */

import { Rng } from "../../rng";

/** Everything needed to reproduce an episode set. */
export interface RetryTraceSpec {
  id: string;
  /** One line, shown on the site and in the CLI. */
  description: string;
  /** Independent episodes simulated. */
  episodes: number;
  /** Exclusive upper bound on an outage, in milliseconds. */
  maxOutageMs: number;
  /** An episode is abandoned once the clock passes this. */
  deadlineMs: number;
  /** Probability an attempt made after the outage still fails, as a percent. */
  flakePercent: number;
  seed: number;
}

/** The canonical retry workload. */
export const RETRY_TRACES: Record<string, RetryTraceSpec> = {
  "outage-30s": {
    id: "outage-30s",
    description:
      "1,000 independent outages of up to 30 seconds, behind a service that still fails one attempt in ten once recovered. The everyday transient failure.",
    episodes: 1_000,
    maxOutageMs: 30_001,
    deadlineMs: 60_000,
    flakePercent: 10,
    seed: 60,
  },
};

/**
 * Seed for an episode's **environment** stream: the outage and the per-attempt
 * success rolls.
 *
 * One stream per episode rather than one for the whole run, so episode 40 faces
 * the same outage whatever the policy did in episode 39.
 */
export function environmentSeed(spec: RetryTraceSpec, episode: number): number {
  return spec.seed + episode;
}

/**
 * Seed for an episode's **policy** stream: whatever jitter the policy draws.
 *
 * Deliberately a separate stream from the environment's. See TRACES.md — this
 * is the one place the implementation departs from the plan's wording, and the
 * reason is that a shared stream would let a policy's own jitter draws shift
 * the outcome of the environment's later coin flips, so two policies would face
 * different luck rather than different strategies.
 */
export function policySeed(spec: RetryTraceSpec, episode: number): number {
  return spec.seed + POLICY_SEED_OFFSET + episode;
}

/**
 * Far enough from any environment seed that the two never collide, for any
 * episode count this domain will plausibly use.
 */
const POLICY_SEED_OFFSET = 1_000_000;

function specFor(id: string): RetryTraceSpec {
  const spec = RETRY_TRACES[id];
  if (spec === undefined) {
    throw new Error(
      `unknown retry trace "${id}". Known: ${Object.keys(RETRY_TRACES).join(", ")}`,
    );
  }
  return spec;
}

/**
 * The outage duration of each episode, in milliseconds.
 *
 * This is the first draw of each episode's environment stream, so it is exactly
 * what the harness will see.
 *
 * @param id one of {@link RETRY_TRACES}.
 * @param maxEvents stop after this many episodes.
 */
export function generateRetryTrace(id: string, maxEvents?: number): Uint32Array {
  const spec = specFor(id);
  const limit = maxEvents === undefined ? spec.episodes : Math.min(maxEvents, spec.episodes);
  const outages = new Uint32Array(limit);

  for (let episode = 0; episode < limit; episode += 1) {
    const rng = new Rng(environmentSeed(spec, episode));
    outages[episode] = rng.nextInt(spec.maxOutageMs);
  }
  return outages;
}
