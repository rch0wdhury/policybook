/**
 * The rate-limiter simulator.
 *
 * Replays a trace against a policy and records what it decided. As with the
 * cache harness this is the smallest thing that can produce the numbers: the
 * loop allocates nothing, and the policy sees exactly the calls its interface
 * promises.
 *
 * It also polices the contract. A `retryAfter` that goes backwards in time, or
 * a `stateSize` that grows without bound, is a bug worth failing on rather than
 * quietly reporting.
 */

import type { RateLimiterPolicy } from "./interface";
import type { RateLimiterTrace } from "./traces";

export interface RateLimiterHarnessOptions {
  /** Exclusive upper bound on key values; sizes the per-key accept table. */
  keyUniverse: number;
  /** Cost charged for each arrival. The canonical traces all use 1. */
  cost?: number;
}

export interface RateLimiterHarnessResult {
  events: number;
  accepted: number;
  denied: number;
  /** The most accepts the policy ever allowed inside one 100 ms window. */
  maxBurst100ms: number;
  /** Keys that appeared at least once in the trace. */
  keysSeen: number;
  /** Sum of per-key accept counts. Equal to `accepted`, kept for the record. */
  acceptSum: number;
  /** Sum of squared per-key accept counts, for Jain's fairness index. */
  acceptSumSquares: number;
  /** Largest `stateSize()` observed, or null if the policy does not report it. */
  entriesTracked: number | null;
}

/** Width of the burst window, in milliseconds. */
const BURST_WINDOW_MS = 100;

/**
 * Run `policy` over `trace`.
 *
 * Per-key accept counts live in a `Uint32Array` indexed by key rather than a
 * `Map`: keys are dense integers by construction, so this is faster and free of
 * per-event allocation.
 */
export function runRateLimiterTrace(
  policy: RateLimiterPolicy,
  trace: RateLimiterTrace,
  options: RateLimiterHarnessOptions,
): RateLimiterHarnessResult {
  const { keyUniverse } = options;
  const cost = options.cost ?? 1;

  if (!Number.isInteger(keyUniverse) || keyUniverse < 1) {
    throw new RangeError(
      `runRateLimiterTrace: keyUniverse must be a positive integer, got ${keyUniverse}`,
    );
  }
  if (!Number.isInteger(cost) || cost < 0) {
    throw new RangeError(
      `runRateLimiterTrace: cost must be a non-negative integer, got ${cost}`,
    );
  }
  if (trace.times.length !== trace.keys.length) {
    throw new RangeError(
      `runRateLimiterTrace: trace has ${trace.times.length} times but ${trace.keys.length} keys`,
    );
  }

  const events = trace.times.length;
  const acceptsByKey = new Uint32Array(keyUniverse);
  const arrivalsByKey = new Uint8Array(keyUniverse);

  // Accept timestamps inside the current burst window, as a ring used strictly
  // as a FIFO. Sized for the worst case — every event accepted — so the loop
  // never grows it.
  const window = new Uint32Array(events);
  let windowHead = 0;
  let windowTail = 0;

  let accepted = 0;
  let denied = 0;
  let maxBurst = 0;
  let entriesTracked: number | null = null;
  let previousTime = 0;

  const stateSize = policy.stateSize?.bind(policy);

  for (let index = 0; index < events; index += 1) {
    const now = trace.times[index]!;
    const key = trace.keys[index]!;

    if (key >= keyUniverse) {
      throw new RangeError(
        `runRateLimiterTrace: event ${index} names key ${key}, outside the key universe ${keyUniverse}`,
      );
    }
    if (now < previousTime) {
      throw new RangeError(
        `runRateLimiterTrace: event ${index} is at ${now} ms, before event ${index - 1} at ${previousTime} ms. ` +
          "A trace must be non-decreasing in time.",
      );
    }
    previousTime = now;
    arrivalsByKey[key] = 1;

    if (policy.allow(key, cost, now)) {
      accepted += 1;
      acceptsByKey[key] = (acceptsByKey[key] ?? 0) + 1;

      // The window is [now - 99, now]: one hundred milliseconds wide, inclusive
      // at both ends, so an accept exactly 100 ms old has left it.
      window[windowTail] = now;
      windowTail += 1;
      while (windowHead < windowTail && window[windowHead]! + BURST_WINDOW_MS <= now) {
        windowHead += 1;
      }
      const inWindow = windowTail - windowHead;
      if (inWindow > maxBurst) maxBurst = inWindow;
    } else {
      denied += 1;
    }

    if (stateSize !== undefined) {
      const size = stateSize();
      if (!Number.isInteger(size) || size < 0) {
        throw new Error(
          `runRateLimiterTrace: policy reported stateSize ${size} at event ${index}. ` +
            "stateSize() must return a non-negative integer.",
        );
      }
      if (entriesTracked === null || size > entriesTracked) entriesTracked = size;
    }
  }

  // Fairness sums are integers until the single division in `metrics.ts`.
  let keysSeen = 0;
  let acceptSum = 0;
  let acceptSumSquares = 0;
  for (let key = 0; key < keyUniverse; key += 1) {
    if (arrivalsByKey[key] !== 1) continue;
    keysSeen += 1;
    const count = acceptsByKey[key]!;
    acceptSum += count;
    acceptSumSquares += count * count;
  }

  return {
    events,
    accepted,
    denied,
    maxBurst100ms: maxBurst,
    keysSeen,
    acceptSum,
    acceptSumSquares,
    entriesTracked,
  };
}
