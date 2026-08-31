/**
 * Canonical traces for the rate-limiter domain.
 *
 * As with the cache traces, these are generated rather than downloaded, and
 * specified precisely enough that the Python and C ports reproduce them event
 * for event from the seed alone. Nothing here reads a file, a
 * clock, or an environment variable.
 *
 * Arrivals are a **per-millisecond Bernoulli process** rather than a Poisson
 * one. A Poisson inter-arrival time needs a
 * logarithm, and `log` is not correctly rounded across C standard libraries, so
 * the three ports would eventually disagree. Walking the clock one millisecond
 * at a time and asking `nextFloat() < p` needs no transcendental function at
 * all, and at these rates the two are indistinguishable in what they exercise.
 *
 * The full prose specification is in TRACES.md next to this file; the code and
 * that document must agree.
 */

import { Rng } from "../../rng";
import { ZipfSampler } from "../../zipf";

/** Everything needed to reproduce a trace and to benchmark against it. */
export interface RateLimiterTraceSpec {
  id: string;
  /** One line, shown on the site and in the CLI. */
  description: string;
  /** Length of the simulated period, in milliseconds. */
  durationMs: number;
  /** Exclusive upper bound on any key the trace emits. */
  keyUniverse: number;
  seed: number;
}

/** The three canonical rate-limiter traces. */
export const RATE_LIMITER_TRACES: Record<string, RateLimiterTraceSpec> = {
  steady: {
    id: "steady",
    description:
      "One key arriving at about 90 requests per second for a minute — just under a 100/s limit. Separates policies that leak from policies that do not.",
    durationMs: 60_000,
    keyUniverse: 1,
    seed: 50,
  },
  bursty: {
    id: "bursty",
    description:
      "One key, silent for 1,800 ms then 200 ms at 500 requests per second, repeating. The shape that separates a token bucket from a leaky bucket.",
    durationMs: 60_000,
    keyUniverse: 1,
    seed: 51,
  },
  "many-keys": {
    id: "many-keys",
    description:
      "Two minutes at 500 requests per second spread over 10,000 keys, Zipf alpha 1.0. Exposes per-key bookkeeping cost and unfairness.",
    durationMs: 120_000,
    keyUniverse: 10_000,
    seed: 52,
  },
  overload: {
    id: "overload",
    description:
      "One key at 300 requests per second for a minute — three times a 100/s limit, sustained. Every correct limiter admits the same total; what differs is the shape.",
    durationMs: 60_000,
    keyUniverse: 1,
    seed: 53,
  },
};

/** Arrival probability per millisecond on the `steady` trace. */
const STEADY_P = 0.09;
/** Arrival probability per millisecond while `bursty` is in its ON phase. */
const BURSTY_P = 0.5;
/** Arrival probability per millisecond on `many-keys`. */
const MANY_KEYS_P = 0.5;
/** Arrival probability per millisecond on `overload` — three times the limit. */
const OVERLOAD_P = 0.3;

/** The `bursty` cycle: this long in total, of which the first part is ON. */
const BURST_CYCLE_MS = 2_000;
const BURST_ON_MS = 200;

/** Distinct keys the `many-keys` Zipf body draws from. */
const MANY_KEYS_KEYSPACE = 10_000;

/**
 * A generated trace: arrival times and the key each arrival names.
 *
 * Two parallel arrays rather than an array of objects, for the same reason the
 * cache trace is a flat `Uint32Array` — a million-event benchmark should not
 * allocate a million objects.
 *
 * There is no per-event cost array: every canonical arrival costs one unit.
 * Costs other than one are a real part of the interface (a dual bucket weighs a
 * request by the tokens it will consume) but they are exercised by hand-written
 * vectors, where the numbers can be reasoned about, rather than by a generated
 * workload where they would be arbitrary.
 */
export interface RateLimiterTrace {
  /** Arrival times in milliseconds, non-decreasing. */
  times: Uint32Array;
  /** The key of each arrival, parallel to `times`. */
  keys: Uint32Array;
}

function specFor(id: string): RateLimiterTraceSpec {
  const spec = RATE_LIMITER_TRACES[id];
  if (spec === undefined) {
    throw new Error(
      `unknown rate-limiter trace "${id}". Known: ${Object.keys(RATE_LIMITER_TRACES).join(", ")}`,
    );
  }
  return spec;
}

/**
 * Generate a trace.
 *
 * @param id one of {@link RATE_LIMITER_TRACES}.
 * @param maxEvents stop after this many arrivals. Generation is sequential and
 *   consumes the random stream in order, so a truncated trace is exactly a
 *   prefix of the full one — which is what lets the site run a short trace and
 *   still be reproducible.
 */
export function generateRateLimiterTrace(id: string, maxEvents?: number): RateLimiterTrace {
  const spec = specFor(id);

  switch (id) {
    case "steady":
      return generateSingleKey(spec, STEADY_P, maxEvents);
    case "bursty":
      return generateBursty(spec, maxEvents);
    case "many-keys":
      return generateManyKeys(spec, maxEvents);
    case "overload":
      // The same shape as `steady`, at a rate the limiter cannot meet.
      return generateSingleKey(spec, OVERLOAD_P, maxEvents);
    default:
      throw new Error(`no generator for trace "${id}"`);
  }
}

/**
 * Trim the working arrays to what was actually written.
 *
 * The arrival count is not known in advance — it is the outcome of the Bernoulli
 * process — so generation writes into arrays sized for the worst case (one
 * arrival every millisecond) and slices at the end.
 */
function finish(times: Uint32Array, keys: Uint32Array, written: number): RateLimiterTrace {
  return { times: times.slice(0, written), keys: keys.slice(0, written) };
}

/**
 * A steady stream on a single key: one Bernoulli draw per millisecond.
 *
 * Shared by `steady` and `overload`, which differ only in the arrival rate —
 * one just under the reference limit and one three times over it.
 */
function generateSingleKey(
  spec: RateLimiterTraceSpec,
  probability: number,
  maxEvents?: number,
): RateLimiterTrace {
  const rng = new Rng(spec.seed);
  const limit = maxEvents ?? Number.POSITIVE_INFINITY;
  const times = new Uint32Array(spec.durationMs);
  const keys = new Uint32Array(spec.durationMs);
  let written = 0;

  for (let t = 0; t < spec.durationMs && written < limit; t += 1) {
    if (rng.nextFloat() < probability) {
      times[written] = t;
      keys[written] = 0;
      written += 1;
    }
  }
  return finish(times, keys, written);
}

/**
 * Silence, then a burst, repeating.
 *
 * **A millisecond in the OFF phase consumes no random draw.** That is the
 * pinned call order and every port must match it: drawing during the silence
 * would be equally valid as a definition and would produce a completely
 * different trace from the same seed.
 */
function generateBursty(spec: RateLimiterTraceSpec, maxEvents?: number): RateLimiterTrace {
  const rng = new Rng(spec.seed);
  const limit = maxEvents ?? Number.POSITIVE_INFINITY;
  const times = new Uint32Array(spec.durationMs);
  const keys = new Uint32Array(spec.durationMs);
  let written = 0;

  for (let t = 0; t < spec.durationMs && written < limit; t += 1) {
    if (t % BURST_CYCLE_MS >= BURST_ON_MS) continue;
    if (rng.nextFloat() < BURSTY_P) {
      times[written] = t;
      keys[written] = 0;
      written += 1;
    }
  }
  return finish(times, keys, written);
}

/**
 * A busy stream spread over a skewed keyspace.
 *
 * The draw order is pinned: the Bernoulli draw comes first, and the Zipf sample
 * is taken **only when that draw produced an arrival**. Sampling a key
 * unconditionally would consume the stream at a different rate and diverge.
 */
function generateManyKeys(spec: RateLimiterTraceSpec, maxEvents?: number): RateLimiterTrace {
  const rng = new Rng(spec.seed);
  const zipf = new ZipfSampler(MANY_KEYS_KEYSPACE, 1);
  const limit = maxEvents ?? Number.POSITIVE_INFINITY;
  const times = new Uint32Array(spec.durationMs);
  const keys = new Uint32Array(spec.durationMs);
  let written = 0;

  for (let t = 0; t < spec.durationMs && written < limit; t += 1) {
    if (rng.nextFloat() < MANY_KEYS_P) {
      times[written] = t;
      keys[written] = zipf.sample(rng);
      written += 1;
    }
  }
  return finish(times, keys, written);
}
