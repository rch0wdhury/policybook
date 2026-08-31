/**
 * The KV-cache simulator.
 *
 * Drives a policy through a decode, one step at a time, and measures what it
 * threw away. As with the other harnesses it is the smallest thing that can
 * produce the numbers, and it polices the contract: a policy that evicts a
 * position it does not hold, or fails to reach the budget, fails loudly here
 * rather than quietly producing a flattering score.
 *
 * The metrics are **proxies**, and the domain README says so at length. This
 * harness measures how much attention mass a policy kept, not how good the
 * model's output was; those correlate, but a policy is not proven by the first.
 */

import type { KvCachePolicy } from "./interface";
import { generateKvCacheTrace } from "./traces";
import type { KvCacheTraceSpec } from "./traces";

export interface KvCacheHarnessOptions {
  /** Maximum positions the policy may retain. */
  budget: number;
  /** Stop after this many decode steps. Defaults to the whole sequence. */
  maxSteps?: number;
}

export interface KvCacheHarnessResult {
  steps: number;
  budget: number;
  /** Attention mass the kept set held, summed over steps. */
  totalRetainedMass: number;
  /** Top-32 positions still held, summed over steps. */
  totalHeavyHits: number;
  /** Top-32 positions that existed at all, summed over steps. */
  totalHeavyPossible: number;
  /** Times `evict` was called. */
  evictionCalls: number;
  /** Positions removed in total. */
  evicted: number;
}

/** How many of the top positions count as heavy hitters, for the recall metric. */
const HEAVY_HITTERS = 32;

/**
 * Run `policy` through the decode described by `spec`.
 *
 * The cache starts holding position 0 — the token that exists before the first
 * decode step — and a policy must start its own bookkeeping the same way. At
 * step `t` the policy is shown the attention the new token paid to each
 * position it still holds — **not renormalised**, so the weights sum to less
 * than one by exactly the mass already lost. Then position `t` joins the kept
 * set, and if that puts it over budget the policy is asked to evict.
 */
export function runKvCacheTrace(
  policy: KvCachePolicy,
  spec: KvCacheTraceSpec,
  options: KvCacheHarnessOptions,
): KvCacheHarnessResult {
  const { budget } = options;
  if (!Number.isInteger(budget) || budget < 1) {
    throw new RangeError(`runKvCacheTrace: budget must be a positive integer, got ${budget}`);
  }

  const maxSteps = options.maxSteps ?? spec.sequenceLength;

  // Kept positions in ascending order, which is the order `attn` is built in
  // and the order every policy is entitled to assume.
  //
  // Position 0 starts kept: its token exists before the first decode step, so
  // the cache holds it from the outset and the first call already shows the
  // policy one weight. Without this no policy could ever retain the heaviest
  // sink, and even an unbounded budget could not reach a retained mass of one.
  const kept: number[] = [0];
  const isKept = new Uint8Array(spec.sequenceLength);
  isKept[0] = 1;
  // One buffer, sliced per step, so a 4,096-step run allocates once.
  const attnBuffer = new Float32Array(spec.sequenceLength);
  // Scratch for the top-k selection below, likewise allocated once.
  const heavyScratch = new Float64Array(HEAVY_HITTERS);

  let steps = 0;
  let totalRetainedMass = 0;
  let totalHeavyHits = 0;
  let totalHeavyPossible = 0;
  let evictionCalls = 0;
  let evicted = 0;

  for (const weights of generateKvCacheTrace(spec.id, maxSteps)) {
    const position = weights.length; // step t attends over 0 .. t-1
    steps += 1;

    // What the policy can still see, in ascending position order.
    for (let index = 0; index < kept.length; index += 1) {
      attnBuffer[index] = weights[kept[index]!]!;
    }
    const attn = attnBuffer.subarray(0, kept.length);

    // Measured before the policy acts: this is the attention the model would
    // actually have had available at this step.
    let retained = 0;
    for (let index = 0; index < kept.length; index += 1) retained += attn[index]!;
    totalRetainedMass += retained;

    const heavyPossible = Math.min(HEAVY_HITTERS, weights.length);
    if (heavyPossible > 0) {
      totalHeavyPossible += heavyPossible;
      const threshold = kthLargest(weights, heavyPossible, heavyScratch);
      totalHeavyHits += countHeavyHitsKept(weights, isKept, heavyPossible, threshold);
    }

    policy.onDecodeStep(position, attn);

    // The new token joins the cache.
    kept.push(position);
    isKept[position] = 1;

    if (kept.length > budget) {
      evictionCalls += 1;
      const dropped = policy.evict(budget);

      for (const victim of dropped) {
        if (!Number.isInteger(victim) || victim < 0 || victim >= spec.sequenceLength) {
          throw new Error(
            `runKvCacheTrace: policy evicted position ${victim} at step ${position}, ` +
              "which is not a valid position.",
          );
        }
        if (isKept[victim] !== 1) {
          throw new Error(
            `runKvCacheTrace: policy evicted position ${victim} at step ${position}, ` +
              "which it does not hold. A policy must only return positions it currently keeps.",
          );
        }
        isKept[victim] = 0;
      }
      evicted += dropped.length;

      // Rebuild the ascending kept list. Filtering rather than splicing keeps
      // this O(kept) per eviction instead of O(kept x dropped).
      let write = 0;
      for (let read = 0; read < kept.length; read += 1) {
        if (isKept[kept[read]!] === 1) {
          kept[write] = kept[read]!;
          write += 1;
        }
      }
      kept.length = write;

      if (kept.length > budget) {
        throw new Error(
          `runKvCacheTrace: policy returned ${dropped.length} position(s) at step ${position}, ` +
            `leaving ${kept.length} kept against a budget of ${budget}. ` +
            "evict() must free enough to reach the budget.",
        );
      }
    }
  }

  return {
    steps,
    budget,
    totalRetainedMass,
    totalHeavyHits,
    totalHeavyPossible,
    evictionCalls,
    evicted,
  };
}

/**
 * The `k`-th largest of `weights`, via a min-heap of the `k` largest seen.
 *
 * `heap` is caller-owned scratch of length ≥ `k`, so a full run allocates
 * nothing per step; sorting all 4,096 weights at every one of 4,095 steps —
 * and boxing them into a fresh array to do it — would dominate the run.
 * O(n log k), and k is at most 32.
 */
function kthLargest(weights: Float32Array, k: number, heap: Float64Array): number {
  let size = 0;
  for (let i = 0; i < weights.length; i += 1) {
    const value = weights[i]!;
    if (size < k) {
      // Push, sifting up.
      heap[size] = value;
      let child = size;
      size += 1;
      while (child > 0) {
        const parent = (child - 1) >> 1;
        if (heap[parent]! <= heap[child]!) break;
        const swap = heap[parent]!;
        heap[parent] = heap[child]!;
        heap[child] = swap;
        child = parent;
      }
    } else if (value > heap[0]!) {
      // Replace the smallest of the current top k, sifting down.
      heap[0] = value;
      let parent = 0;
      for (;;) {
        const left = 2 * parent + 1;
        const right = left + 1;
        let least = parent;
        if (left < k && heap[left]! < heap[least]!) least = left;
        if (right < k && heap[right]! < heap[least]!) least = right;
        if (least === parent) break;
        const swap = heap[parent]!;
        heap[parent] = heap[least]!;
        heap[least] = swap;
        parent = least;
      }
    }
  }
  return heap[0]!;
}

/**
 * How many of the `count` heaviest positions the policy still holds.
 *
 * Ties at the threshold go to the lower position, which is arbitrary but fixed
 * — without a rule the metric would depend on sort stability and stop being
 * reproducible. A position strictly above the threshold is always in the top
 * set; only the tied positions compete for the remaining slots.
 */
function countHeavyHitsKept(
  weights: Float32Array,
  isKept: Uint8Array,
  count: number,
  threshold: number,
): number {
  let above = 0;
  for (let i = 0; i < weights.length; i += 1) {
    if (weights[i]! > threshold) above += 1;
  }

  let tieSlots = count - above;
  let hits = 0;
  for (let position = 0; position < weights.length; position += 1) {
    const weight = weights[position]!;
    if (weight > threshold) {
      if (isKept[position] === 1) hits += 1;
    } else if (weight === threshold && tieSlots > 0) {
      tieSlots -= 1;
      if (isKept[position] === 1) hits += 1;
    }
  }
  return hits;
}
