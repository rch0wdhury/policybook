/**
 * The cache simulator.
 *
 * Drives a policy over a trace and counts what happened. It is deliberately the
 * smallest thing that can produce a hit rate: residency is a byte per key, the
 * loop allocates nothing, and the policy sees exactly the calls its interface
 * promises — no more (concept.md §2, non-goals: this is not a general-purpose
 * simulator).
 *
 * The harness also polices the contract. A policy that evicts a key it does not
 * hold, or fails to free space, fails loudly here rather than quietly producing
 * a wrong hit rate.
 */

import type { CachePolicy } from "./interface";

export interface CacheHarnessOptions {
  /** Maximum entries the cache holds. */
  capacity: number;
  /** Exclusive upper bound on key values; sizes the residency table. */
  keyUniverse: number;
}

export interface CacheHarnessResult {
  events: number;
  hits: number;
  misses: number;
  evictions: number;
  /** Misses where the policy's `admit` declined to insert the key. */
  admissionRejections: number;
}

/**
 * Run `policy` over `trace`.
 *
 * Residency is tracked in a `Uint8Array` indexed by key rather than a `Set`.
 * Keys are dense integers by construction, so this is both faster and free of
 * per-event allocation, which matters when a bench run is a million events
 * across a dozen policies.
 */
export function runCacheTrace(
  policy: CachePolicy<number>,
  trace: Uint32Array,
  options: CacheHarnessOptions,
): CacheHarnessResult {
  const { capacity, keyUniverse } = options;

  if (!Number.isInteger(capacity) || capacity < 1) {
    throw new RangeError(`runCacheTrace: capacity must be a positive integer, got ${capacity}`);
  }
  if (!Number.isInteger(keyUniverse) || keyUniverse < 1) {
    throw new RangeError(
      `runCacheTrace: keyUniverse must be a positive integer, got ${keyUniverse}`,
    );
  }

  const resident = new Uint8Array(keyUniverse);
  let residentCount = 0;

  let hits = 0;
  let misses = 0;
  let evictions = 0;
  let admissionRejections = 0;

  // Hoisted so the loop makes no property lookups and creates no closures.
  const admit = policy.admit?.bind(policy);

  for (let index = 0; index < trace.length; index += 1) {
    const key = trace[index]!;
    if (key >= keyUniverse) {
      throw new RangeError(
        `runCacheTrace: trace event ${index} is key ${key}, outside the key universe ${keyUniverse}`,
      );
    }

    const hit = resident[key] === 1;

    // The policy learns about the access before any insertion (concept.md §5.1).
    policy.onAccess(key, hit);

    if (hit) {
      hits += 1;
      continue;
    }
    misses += 1;

    if (admit !== undefined && !admit(key)) {
      admissionRejections += 1;
      continue;
    }

    resident[key] = 1;
    residentCount += 1;

    while (residentCount > capacity) {
      const victim = policy.evict();
      if (victim < 0 || victim >= keyUniverse || resident[victim] !== 1) {
        throw new Error(
          `runCacheTrace: policy evicted key ${victim} at event ${index}, which it does not hold. ` +
            "A policy must only return a resident key from evict().",
        );
      }
      resident[victim] = 0;
      residentCount -= 1;
      evictions += 1;
    }
  }

  return { events: trace.length, hits, misses, evictions, admissionRejections };
}
