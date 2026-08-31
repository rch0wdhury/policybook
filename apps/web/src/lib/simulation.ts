/**
 * Stepping a policy through a trace, one event at a time.
 *
 * The harnesses in `@policybook/core` run a whole trace and return metrics,
 * which is what a benchmark wants and not what a runner wants — a runner has to
 * stop, show what just happened, and go back. So this drives the *same*
 * policies over the *same* traces, one step at a time.
 *
 * **The numbers it produces must equal the committed benchmark's exactly.** A
 * site whose runner disagreed with its own tables would be worse than one with
 * no runner at all, so `simulation.test.ts` runs each stepper to completion and
 * compares against the core harness event for event. That test is the only
 * thing making this duplication safe.
 *
 * ## Going backwards
 *
 * There is no `serialize()`/`restore()` on policies, and there does not need to
 * be. Every policy here is deterministic and every trace comes from a seed, so
 * "the state at step N" is exactly "a fresh policy, replayed N times" — which
 * is not an approximation of the real state, it *is* the real state.
 *
 * Measured on the path that actually pays for it — a backwards seek, which
 * reuses the already-generated trace: **2 ms to replay 20,000 cache events**,
 * and 9-13 ms for the 62,500 of the shifting-popularity prefix. Forward
 * stepping, which is what a reader does almost all of the time, costs nothing
 * extra at all. A serialisation API across 30 policies in three languages would
 * have bought those few milliseconds and added a surface that could drift from
 * the behaviour it claims to capture.
 *
 * (kv-cache is the exception: replaying its 4,096-step decode trace costs
 * ~270 ms, because every step recomputes attention over the kept set. That is
 * the one place where stepping backwards is genuinely slow, and the one place
 * where snapshots might yet earn their keep.)
 */

import {
  CACHE_TRACES,
  SCAN_INTERVAL,
  SHIFT_INTERVAL,
  cacheMetrics,
  generateCacheTrace,
  type CachePolicy,
} from "../../../../packages/core/src/domains/cache";
import {
  KV_CACHE_TRACES,
  generateKvCacheTrace,
  kvCacheMetrics,
  type KvCachePolicy,
} from "../../../../packages/core/src/domains/kv-cache";
import {
  RATE_LIMITER_TRACES,
  generateRateLimiterTrace,
  rateLimiterMetrics,
  type RateLimiterPolicy,
} from "../../../../packages/core/src/domains/rate-limiter";

/** What a policy constructor looks like from here. */
export type PolicyFactory = (params: Record<string, unknown>) => unknown;

/** Everything a visualisation needs after one step. */
export interface Frame {
  step: number;
  totalSteps: number;
  metrics: Record<string, number>;
  /** Domain-specific; each visualisation knows its own shape. */
  view: CacheView | RateLimiterView | KvCacheView;
}

export interface CacheView {
  kind: "cache";
  /** The key just requested. */
  key: number;
  hit: boolean;
  /** Resident keys, in whatever order the policy holds them. */
  resident: number[];
  /**
   * One annotation per resident key, index-aligned, or null where the policy
   * offers none.
   *
   * Every cache policy keeps some per-entry bit that decides who dies — SIEVE's
   * visited flag, CLOCK's reference bit, LFU's counter, 2Q's queue — and it is
   * the most interesting thing about the policy. Rather than hard-code one
   * shape, the simulation asks each policy for whichever introspection method it
   * happens to expose, which are the same methods the vectors call. So the
   * picture shows the policy's real decision state rather than something
   * reconstructed for display.
   */
  annotations: (string | number | null)[];
  /** What the annotations mean, for the legend. Empty when there are none. */
  annotationLabel: string;
  /** Evicted on this step, or null. */
  evicted: number | null;
  capacity: number;
  /** Hit rate sampled over time, for the line chart. */
  history: number[];
}

/** Introspection methods a cache policy may expose, in preference order. */
const CACHE_ANNOTATIONS: { method: string; label: string; bit?: boolean }[] = [
  { method: "isVisited", label: "visited bit", bit: true },
  { method: "isReferenced", label: "reference bit", bit: true },
  { method: "frequencyOf", label: "frequency" },
  { method: "queueOf", label: "queue" },
  { method: "segmentOf", label: "segment" },
  { method: "listOfKey", label: "list" },
];

/** Sample the hit rate this often, so the history stays small enough to post. */
const HISTORY_EVERY = 50;

/**
 * Per-key budget gauges a rate-limiter policy may expose, in preference order.
 *
 * These are the same introspection methods the vectors call, so the picture
 * shows the number the policy actually decides on.
 */
const RATE_LIMITER_GAUGES: { method: string; label: string }[] = [
  { method: "tokensOf", label: "tokens" },
  { method: "levelOf", label: "level" },
  { method: "countOf", label: "count in window" },
  { method: "estimateOf", label: "weighted estimate" },
  { method: "requestsOf", label: "requests" },
];

/** How many keys the multi-key view follows. */
const TOP_KEYS = 8;

/**
 * Sample the retained mass this often.
 *
 * Finer than the cache's, because a decode trace is a few thousand steps rather
 * than tens of thousands and the interesting part — the collapse when a policy
 * drops something it needed — happens over tens of steps.
 */
const KV_HISTORY_EVERY = 10;

export interface RateLimiterView {
  kind: "rate-limiter";
  time: number;
  key: number;
  allowed: boolean;
  /** Recent decisions, oldest first, for a strip chart. */
  recent: { time: number; allowed: boolean }[];
  /**
   * The policy's own budget gauge for the focused key, sampled over time.
   *
   * Every limiter here keeps some per-key quantity that decides the next
   * answer — a token count, a bucket level, a window tally, a rate estimate —
   * under its own name. As with the cache grid, the picture asks each policy
   * for whichever it exposes rather than reconstructing something for display.
   *
   * A list rather than one, because dual-bucket keeps *two* and showing one of
   * them would hide the interaction that is the entire point of the policy.
   */
  gauges: { label: string; values: number[] }[];
  /** The time each gauge sample was taken at, index-aligned with the values. */
  gaugeTimes: number[];
  /**
   * The key the gauges follow: the busiest one, fixed for the whole run.
   *
   * It has to be fixed. Sampling whichever key just arrived would, on a
   * 10,000-key trace, plot one point from each of ten thousand different
   * buckets and call the result a budget over time.
   */
  gaugeKey: number;
  /**
   * How long the policy says the focused key must wait, in ms, or null when it
   * offers no hint. Zero means "try now", so a run of zeros beside rejections
   * is a policy contradicting itself and worth being able to see.
   */
  retryAfter: number | null;
  /**
   * The busiest keys in the trace, most traffic first, with their tallies.
   *
   * Single-key traces leave this empty. For `many-keys` it is the top 8, which
   * is where fairness between keys becomes visible: a limiter with one global
   * bucket lets whoever arrives first take everything.
   */
  topKeys: { key: number; arrivals: number; accepted: number }[];
  /**
   * Accept rate sampled over time.
   *
   * The domain's headline number as a series, so the compare page can overlay
   * policies on one chart. Every runnable domain exposes `history` meaning
   * "the metric this domain is judged by, so far", which is what lets one
   * overlay renderer serve all three.
   */
  history: number[];
}

export interface KvCacheView {
  kind: "kv-cache";
  position: number;
  /** Kept positions, ascending. */
  kept: number[];
  /** Attention over the kept positions, index-aligned. */
  attention: number[];
  evicted: number[];
  budget: number;
  /**
   * Retained attention mass, sampled over time.
   *
   * The share of the model's attention that survived the policy's choices. It
   * is the number the whole domain turns on: a policy holding the right 512
   * positions keeps nearly all the mass, and one holding the wrong 512 keeps
   * very little while looking equally full.
   */
  history: number[];
  /** How far into the sequence we are, for the strip's scale. */
  sequenceLength: number;
}

export interface Simulation {
  readonly totalSteps: number;
  readonly step: number;
  /** Move to exactly this step, forwards or backwards. */
  seek(step: number): void;
  frame(): Frame;
}

/**
 * How many events a runner replays, for a trace with no internal schedule.
 *
 * The full traces run to 100,000 and 1,000,000 events, which is more than a
 * reader will ever scrub through and more than is honest to replay on every
 * backwards seek.
 */
export const REDUCED_LENGTH = 20_000;

/**
 * How many events to replay for a given trace.
 *
 * **A prefix has to be long enough to contain the thing the trace is named
 * after.** Two of the cache traces do something on a schedule: `scan-heavy`
 * injects a scan every 20,000 accesses, and `shifting-popularity` rotates its
 * hot set every 25,000. Cut either at 20,000 and the runner replays a trace in
 * which the scan never happens and the popularity never shifts — plain Zipf
 * under another name, sitting beside a benchmark table that reports the real
 * thing. Measured, at 20,000 events SIEVE and LFU were identical on every cache
 * trace; over the full shifting-popularity trace they finish at 0.610 and
 * 0.323. The runner was hiding its own subject matter.
 *
 * So a scheduled trace gets enough events for the behaviour to happen twice and
 * be paid for: the second occurrence is where the reader sees the policies come
 * apart, and the first alone can look like a blip.
 *
 * The rule deliberately covers *scheduled* behaviour only, not eviction
 * pressure. `zipf-0.75-1m` holds a 10,000-entry cache, so 20,000 events barely
 * fill it and several policies finish within 0.001 of each other. That prefix
 * is unrepresentative, but it is not *dishonest* — it is a stationary trace
 * shown early, not a scan-heavy trace with no scan. Widening the spread to a
 * useful 0.05 needed 200,000 events and a 282 ms replay on every backwards
 * seek, which is a worse runner than a narrow spread is.
 */
export function reducedLength(domain: string, traceId: string): number {
  if (domain !== "cache") return REDUCED_LENGTH;

  const period =
    traceId === "scan-heavy" ? SCAN_INTERVAL : traceId === "shifting-popularity" ? SHIFT_INTERVAL : 0;
  if (period === 0) return REDUCED_LENGTH;

  const wanted = period * 2 + period / 2;
  return Math.min(wanted, CACHE_TRACES[traceId]?.events ?? wanted);
}

/* -------------------------------------------------------------------------- */
/* cache                                                                      */
/* -------------------------------------------------------------------------- */

class CacheSimulation implements Simulation {
  readonly totalSteps: number;
  step = 0;

  private policy: CachePolicy<number>;
  private readonly trace: Uint32Array;
  private readonly capacity: number;
  private readonly keyUniverse: number;
  private resident: Uint8Array;
  private residentCount = 0;
  private order: number[] = [];

  private hits = 0;
  private misses = 0;
  private evictions = 0;
  private admissionRejections = 0;
  private lastEvicted: number | null = null;
  private lastKey = 0;
  private lastHit = false;
  private history: number[] = [];
  /** Resolved once at construction: the policy either has one or it does not. */
  private readonly annotation: { method: string; label: string; bit?: boolean } | null;

  constructor(
    private readonly factory: PolicyFactory,
    private readonly params: Record<string, unknown>,
    traceId: string,
    length: number,
  ) {
    const spec = CACHE_TRACES[traceId];
    if (spec === undefined) throw new Error(`unknown cache trace "${traceId}"`);

    this.capacity = (params["capacity"] as number | undefined) ?? spec.capacity;
    this.keyUniverse = spec.keyUniverse;
    this.trace = generateCacheTrace(traceId, length);
    this.totalSteps = this.trace.length;

    this.policy = factory({ ...params, capacity: this.capacity }) as CachePolicy<number>;
    this.resident = new Uint8Array(this.keyUniverse);

    const holder = this.policy as unknown as Record<string, unknown>;
    this.annotation =
      CACHE_ANNOTATIONS.find((entry) => typeof holder[entry.method] === "function") ?? null;
  }

  private reset(): void {
    this.policy = this.factory({
      ...this.params,
      capacity: this.capacity,
    }) as CachePolicy<number>;
    this.resident = new Uint8Array(this.keyUniverse);
    this.residentCount = 0;
    this.order = [];
    this.hits = 0;
    this.misses = 0;
    this.evictions = 0;
    this.admissionRejections = 0;
    this.lastEvicted = null;
    this.lastKey = 0;
    this.lastHit = false;
    this.history = [];
    this.step = 0;
  }

  /** One event, mirroring `runCacheTrace` exactly. */
  private advance(): void {
    const key = this.trace[this.step]!;
    const hit = this.resident[key] === 1;

    this.policy.onAccess(key, hit);
    this.lastKey = key;
    this.lastHit = hit;
    this.lastEvicted = null;
    this.step += 1;

    if (hit) this.hits += 1;
    else this.misses += 1;

    if (this.step % HISTORY_EVERY === 0) {
      this.history.push(this.hits / this.step);
    }

    if (hit) return;

    if (this.policy.admit !== undefined && !this.policy.admit(key)) {
      this.admissionRejections += 1;
      return;
    }

    this.resident[key] = 1;
    this.residentCount += 1;
    this.order.push(key);

    while (this.residentCount > this.capacity) {
      const victim = this.policy.evict();
      this.resident[victim] = 0;
      this.residentCount -= 1;
      this.evictions += 1;
      this.lastEvicted = victim;
      const at = this.order.indexOf(victim);
      if (at !== -1) this.order.splice(at, 1);
    }
  }

  seek(step: number): void {
    const target = Math.max(0, Math.min(step, this.totalSteps));
    // Backwards is a replay from the beginning: exact, and cheap enough that
    // nothing more clever is warranted. See the note at the top of the file.
    if (target < this.step) this.reset();
    while (this.step < target) this.advance();
  }

  /**
   * Ask the policy about each resident key, if it will say anything.
   *
   * Wrapped, because these are optional methods a policy is free to implement
   * however it likes: one throwing on a key it considers absent must not take
   * the whole runner down with it.
   */
  private annotationsFor(keys: number[]): (string | number | null)[] {
    const entry = this.annotation;
    if (entry === null) return keys.map(() => null);

    const holder = this.policy as unknown as Record<string, (key: number) => unknown>;
    return keys.map((key) => {
      try {
        const value = holder[entry.method]!(key);
        if (entry.bit === true) return value === true ? "1" : "0";
        if (typeof value === "number" || typeof value === "string") return value;
        return null;
      } catch {
        return null;
      }
    });
  }

  frame(): Frame {
    return {
      step: this.step,
      totalSteps: this.totalSteps,
      metrics: cacheMetrics({
        // `events` is what the hit rate divides by. Leaving it out produced a
        // NaN rather than an error, which is exactly the kind of thing the
        // parity test exists to catch.
        events: this.step,
        hits: this.hits,
        misses: this.misses,
        evictions: this.evictions,
        admissionRejections: this.admissionRejections,
      }) as unknown as Record<string, number>,
      view: {
        kind: "cache",
        key: this.lastKey,
        hit: this.lastHit,
        resident: [...this.order],
        annotations: this.annotationsFor(this.order),
        annotationLabel: this.annotation?.label ?? "",
        evicted: this.lastEvicted,
        capacity: this.capacity,
        history: [...this.history],
      },
    };
  }
}

/* -------------------------------------------------------------------------- */
/* rate-limiter                                                               */
/* -------------------------------------------------------------------------- */

/** How many recent decisions a rate-limiter view keeps for its strip. */
const RECENT_WINDOW = 240;

/** Width of the burst window, matching the core harness. */
const BURST_WINDOW_MS = 100;

class RateLimiterSimulation implements Simulation {
  readonly totalSteps: number;
  step = 0;

  private policy: RateLimiterPolicy;
  private readonly times: Uint32Array;
  private readonly keys: Uint32Array;
  private readonly keyUniverse: number;

  private accepted = 0;
  private denied = 0;
  private acceptsByKey: Uint32Array;
  private arrivalsByKey: Uint8Array;
  /** Accept timestamps still inside the 100 ms burst window, as a FIFO. */
  private burstWindow: Uint32Array;
  private windowHead = 0;
  private windowTail = 0;
  private maxBurst = 0;
  private entriesTracked: number | null = null;

  private recent: { time: number; allowed: boolean }[] = [];
  private lastAllowed = false;
  private gaugeSamples: number[][] = [];
  private gaugeTimes: number[] = [];
  private history: number[] = [];
  /** Resolved once: whichever budget gauges this policy exposes. */
  private readonly gauges: { method: string; label: string }[];
  /** Arrivals per key over the whole trace, for picking the busiest. */
  private readonly arrivalTotals: Map<number, number>;
  /** The key the gauges follow, chosen once. */
  private readonly gaugeKey: number;

  constructor(
    private readonly factory: PolicyFactory,
    private readonly params: Record<string, unknown>,
    traceId: string,
    length: number,
  ) {
    const spec = RATE_LIMITER_TRACES[traceId];
    if (spec === undefined) throw new Error(`unknown rate-limiter trace "${traceId}"`);

    const trace = generateRateLimiterTrace(traceId, length);
    this.times = trace.times;
    this.keys = trace.keys;
    this.keyUniverse = spec.keyUniverse;
    this.totalSteps = this.times.length;

    this.policy = factory(params) as RateLimiterPolicy;
    this.acceptsByKey = new Uint32Array(this.keyUniverse);
    this.arrivalsByKey = new Uint8Array(this.keyUniverse);
    this.burstWindow = new Uint32Array(this.totalSteps);

    const holder = this.policy as unknown as Record<string, unknown>;
    this.gauges = RATE_LIMITER_GAUGES.filter(
      (entry) => typeof holder[entry.method] === "function",
    );
    this.gaugeSamples = this.gauges.map(() => []);

    // Traffic per key over the whole trace, so the busiest are known before
    // stepping and the picture does not reshuffle as the reader scrubs.
    this.arrivalTotals = new Map();
    for (const key of this.keys) {
      this.arrivalTotals.set(key, (this.arrivalTotals.get(key) ?? 0) + 1);
    }

    let busiest = this.keys[0] ?? 0;
    let most = -1;
    for (const [key, count] of this.arrivalTotals) {
      if (count > most || (count === most && key < busiest)) {
        busiest = key;
        most = count;
      }
    }
    this.gaugeKey = busiest;
  }

  private reset(): void {
    this.policy = this.factory(this.params) as RateLimiterPolicy;
    this.accepted = 0;
    this.denied = 0;
    this.acceptsByKey = new Uint32Array(this.keyUniverse);
    this.arrivalsByKey = new Uint8Array(this.keyUniverse);
    this.burstWindow = new Uint32Array(this.totalSteps);
    this.windowHead = 0;
    this.windowTail = 0;
    this.maxBurst = 0;
    this.entriesTracked = null;
    this.recent = [];
    this.lastAllowed = false;
    this.gaugeSamples = this.gauges.map(() => []);
    this.gaugeTimes = [];
    this.history = [];
    this.step = 0;
  }

  /** One arrival, mirroring `runRateLimiterTrace` exactly. */
  private advance(): void {
    const time = this.times[this.step]!;
    const key = this.keys[this.step]!;

    this.arrivalsByKey[key] = 1;
    const allowed = this.policy.allow(key, 1, time);

    this.step += 1;
    this.lastAllowed = allowed;

    if (allowed) {
      this.accepted += 1;
      this.acceptsByKey[key] = (this.acceptsByKey[key] ?? 0) + 1;

      // The window is [now - 99, now]: 100 ms wide and inclusive at both ends,
      // so an accept exactly 100 ms old has already left it.
      this.burstWindow[this.windowTail] = time;
      this.windowTail += 1;
      while (
        this.windowHead < this.windowTail &&
        this.burstWindow[this.windowHead]! + BURST_WINDOW_MS <= time
      ) {
        this.windowHead += 1;
      }
      const inWindow = this.windowTail - this.windowHead;
      if (inWindow > this.maxBurst) this.maxBurst = inWindow;
    } else {
      this.denied += 1;
    }

    const size = this.policy.stateSize?.();
    if (size !== undefined && (this.entriesTracked === null || size > this.entriesTracked)) {
      this.entriesTracked = size;
    }

    this.recent.push({ time, allowed });
    if (this.recent.length > RECENT_WINDOW) this.recent.shift();

    if (this.step % HISTORY_EVERY === 0) {
      this.history.push(this.accepted / this.step);
    }

    // Sampled after the decision, so the gauge shows what the request left
    // behind rather than what it found.
    if (this.gauges.length > 0) {
      this.gaugeTimes.push(time);
      for (let index = 0; index < this.gauges.length; index += 1) {
        this.gaugeSamples[index]!.push(this.gaugeAt(index, this.gaugeKey, time));
      }
      if (this.gaugeTimes.length > RECENT_WINDOW) {
        this.gaugeTimes.shift();
        for (const series of this.gaugeSamples) series.shift();
      }
    }
  }

  /**
   * Read one gauge, tolerating a policy that declines.
   *
   * These are optional methods a policy implements however it likes; one that
   * throws on a key it has never seen must not take the runner down.
   */
  private gaugeAt(index: number, key: number, now: number): number {
    const holder = this.policy as unknown as Record<string, (key: number, now: number) => unknown>;
    try {
      const value = holder[this.gauges[index]!.method]!(key, now);
      return typeof value === "number" && Number.isFinite(value) ? value : 0;
    } catch {
      return 0;
    }
  }

  /** The busiest keys, with what each was granted. Empty for a single-key trace. */
  private topKeys(): { key: number; arrivals: number; accepted: number }[] {
    if (this.arrivalTotals.size < 2) return [];
    return [...this.arrivalTotals.entries()]
      .sort((a, b) => b[1] - a[1] || a[0] - b[0])
      .slice(0, TOP_KEYS)
      .map(([key, arrivals]) => ({
        key,
        arrivals,
        accepted: this.acceptsByKey[key] ?? 0,
      }));
  }

  seek(step: number): void {
    const target = Math.max(0, Math.min(step, this.totalSteps));
    if (target < this.step) this.reset();
    while (this.step < target) this.advance();
  }

  /** The same shape `runRateLimiterTrace` returns, so the metrics agree. */
  private result() {
    let keysSeen = 0;
    let acceptSum = 0;
    let acceptSumSquares = 0;

    for (let key = 0; key < this.keyUniverse; key += 1) {
      if (this.arrivalsByKey[key] === 1) keysSeen += 1;
      const accepts = this.acceptsByKey[key]!;
      acceptSum += accepts;
      acceptSumSquares += accepts * accepts;
    }

    return {
      events: this.step,
      accepted: this.accepted,
      denied: this.denied,
      maxBurst100ms: this.maxBurst,
      keysSeen,
      acceptSum,
      acceptSumSquares,
      entriesTracked: this.entriesTracked,
    };
  }

  frame(): Frame {
    return {
      step: this.step,
      totalSteps: this.totalSteps,
      metrics: rateLimiterMetrics(this.result()) as unknown as Record<string, number>,
      view: {
        kind: "rate-limiter",
        time: this.step > 0 ? this.times[this.step - 1]! : 0,
        key: this.step > 0 ? this.keys[this.step - 1]! : 0,
        allowed: this.lastAllowed,
        recent: [...this.recent],
        gauges: this.gauges.map((entry, index) => ({
          label: entry.label,
          values: [...this.gaugeSamples[index]!],
        })),
        gaugeTimes: [...this.gaugeTimes],
        gaugeKey: this.gaugeKey,
        retryAfter: this.retryAfterNow(),
        topKeys: this.topKeys(),
        history: [...this.history],
      },
    };
  }

  /** What the policy says the last-seen key must wait, if it will say. */
  private retryAfterNow(): number | null {
    if (this.step === 0 || this.policy.retryAfter === undefined) return null;
    try {
      const value = this.policy.retryAfter(this.keys[this.step - 1]!, this.times[this.step - 1]!);
      return typeof value === "number" && Number.isFinite(value) ? value : null;
    } catch {
      return null;
    }
  }
}

/* -------------------------------------------------------------------------- */
/* kv-cache                                                                   */
/* -------------------------------------------------------------------------- */

class KvCacheSimulation implements Simulation {
  readonly totalSteps: number;
  step = 0;

  private policy: KvCachePolicy;
  private readonly steps: Float32Array[];
  private readonly budget: number;
  private readonly sequenceLength: number;

  private kept: number[] = [0];
  private isKept: Uint8Array;
  private retained = 0;
  private heavyHits = 0;
  private heavyPossible = 0;
  private evictionCalls = 0;
  private lastEvicted: number[] = [];
  /**
   * This step's attention weights, indexed by *position*.
   *
   * Not by index into `kept`. The vector the policy is handed covers the
   * positions held *before* this step, and eviction then changes that list, so
   * an array aligned to the old list and read against the new one paints each
   * position with a neighbour's weight. Keeping it position-indexed makes the
   * alignment impossible to get wrong.
   */
  private lastWeights: Float32Array = new Float32Array(0);
  private history: number[] = [];

  constructor(
    private readonly factory: PolicyFactory,
    private readonly params: Record<string, unknown>,
    traceId: string,
    length: number,
  ) {
    const spec = KV_CACHE_TRACES[traceId];
    if (spec === undefined) throw new Error(`unknown kv-cache trace "${traceId}"`);

    this.budget = (params["budget"] as number | undefined) ?? 512;
    this.sequenceLength = spec.sequenceLength;
    // Materialised once: the generator is sequential, so replaying it for every
    // step-back would cost far more than the steps themselves.
    this.steps = [...generateKvCacheTrace(traceId, Math.min(length, spec.sequenceLength - 1))];
    this.totalSteps = this.steps.length;

    this.policy = factory({ ...params, budget: this.budget }) as KvCachePolicy;
    this.isKept = new Uint8Array(spec.sequenceLength);
    this.isKept[0] = 1;
  }

  private reset(): void {
    this.policy = this.factory({ ...this.params, budget: this.budget }) as KvCachePolicy;
    this.kept = [0];
    this.isKept = new Uint8Array(this.sequenceLength);
    this.isKept[0] = 1;
    this.retained = 0;
    this.heavyHits = 0;
    this.heavyPossible = 0;
    this.evictionCalls = 0;
    this.lastEvicted = [];
    this.lastWeights = new Float32Array(0);
    this.history = [];
    this.step = 0;
  }

  private advance(): void {
    const weights = this.steps[this.step]!;
    const position = weights.length;

    const attn = new Float32Array(this.kept.length);
    let retained = 0;
    for (let i = 0; i < this.kept.length; i += 1) {
      attn[i] = weights[this.kept[i]!]!;
      retained += attn[i]!;
    }
    this.retained += retained;
    this.lastWeights = weights;

    const possible = Math.min(32, weights.length);
    if (possible > 0) {
      this.heavyPossible += possible;
      this.heavyHits += this.countHeavyHits(weights, possible);
    }

    this.policy.onDecodeStep(position, attn);
    this.kept.push(position);
    this.isKept[position] = 1;
    this.lastEvicted = [];
    this.step += 1;

    // Sampled every step until the trace is long enough to need thinning: the
    // decode traces are a few thousand steps, not tens of thousands.
    if (this.step % KV_HISTORY_EVERY === 0) {
      this.history.push(this.retained / this.step);
    }

    if (this.kept.length > this.budget) {
      this.evictionCalls += 1;
      const dropped = this.policy.evict(this.budget);
      for (const victim of dropped) this.isKept[victim] = 0;
      this.lastEvicted = [...dropped];
      this.kept = this.kept.filter((pos) => this.isKept[pos] === 1);
    }
  }

  /** Mirrors the harness's top-k selection, ties to the lower position. */
  private countHeavyHits(weights: Float32Array, count: number): number {
    const sorted = Array.from(weights).sort((a, b) => b - a);
    const threshold = sorted[count - 1]!;

    let above = 0;
    for (let i = 0; i < weights.length; i += 1) if (weights[i]! > threshold) above += 1;

    let tieSlots = count - above;
    let hits = 0;
    for (let position = 0; position < weights.length; position += 1) {
      const weight = weights[position]!;
      if (weight > threshold) {
        if (this.isKept[position] === 1) hits += 1;
      } else if (weight === threshold && tieSlots > 0) {
        tieSlots -= 1;
        if (this.isKept[position] === 1) hits += 1;
      }
    }
    return hits;
  }

  seek(step: number): void {
    const target = Math.max(0, Math.min(step, this.totalSteps));
    if (target < this.step) this.reset();
    while (this.step < target) this.advance();
  }

  frame(): Frame {
    return {
      step: this.step,
      totalSteps: this.totalSteps,
      metrics: kvCacheMetrics({
        steps: this.step,
        budget: this.budget,
        totalRetainedMass: this.retained,
        totalHeavyHits: this.heavyHits,
        totalHeavyPossible: this.heavyPossible,
        evictionCalls: this.evictionCalls,
        evicted: 0,
      }) as unknown as Record<string, number>,
      view: {
        kind: "kv-cache",
        position: this.step,
        kept: [...this.kept],
        // Looked up per position, so this is aligned with `kept` by
        // construction. The newest position has no weight of its own, being
        // the query, and reads as zero.
        attention: this.kept.map((position) =>
          position < this.lastWeights.length ? this.lastWeights[position]! : 0,
        ),
        evicted: this.lastEvicted,
        budget: this.budget,
        history: [...this.history],
        sequenceLength: this.sequenceLength,
      },
    };
  }
}

/* -------------------------------------------------------------------------- */

/** Build a simulation for a domain. Throws on an unknown one. */
export function createSimulation(
  domain: string,
  factory: PolicyFactory,
  params: Record<string, unknown>,
  traceId: string,
  length: number = reducedLength(domain, traceId),
): Simulation {
  // A non-positive length is always a caller's mistake, and a silent one: it
  // produces an empty trace whose metrics are all zero, which reads as a policy
  // that admitted nothing rather than as a run that never happened. The bench
  // parity test hit exactly this by passing 0 to mean "full length".
  if (!Number.isInteger(length) || length <= 0) {
    throw new Error(
      `createSimulation: length must be a positive integer, got ${length}. ` +
        `Omit it to use the domain's default for "${traceId}".`,
    );
  }

  switch (domain) {
    case "cache":
      return new CacheSimulation(factory, params, traceId, length);
    case "rate-limiter":
      return new RateLimiterSimulation(factory, params, traceId, length);
    case "kv-cache":
      return new KvCacheSimulation(factory, params, traceId, length);
    default:
      // retry is episodic rather than a stream of events, so it has no
      // step-through runner. Its page shows the benchmark instead.
      throw new Error(`no runner for the "${domain}" domain`);
  }
}

/** The domains a runner exists for. */
export const RUNNABLE_DOMAINS = ["cache", "rate-limiter", "kv-cache"] as const;
