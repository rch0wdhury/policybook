import { describe, expect, it } from "vitest";
import {
  CACHE_TRACES,
  cacheMetrics,
  generateCacheTrace,
  runCacheTrace,
} from "../../../packages/core/src/domains/cache";
import { formatFailures, runVectors } from "../../../packages/vectors/src/run";
import type { VectorsFile } from "../../../packages/vectors/src/types";
import Fifo from "../fifo/index";
import Lru from "../lru/index";
import S3Fifo from "./index";
import vectors from "./vectors.json";

describe("cache/s3-fifo", () => {
  it("passes its vectors", () => {
    const result = runVectors((params) => new S3Fifo(params), vectors as VectorsFile);
    if (result.failures.length > 0) throw new Error(formatFailures(result));
    expect(result.assertionsRun).toBeGreaterThan(0);
  });

  it("rejects nonsense parameters", () => {
    expect(() => new S3Fifo({ capacity: 1 })).toThrow(/at least 2/);
    expect(() => new S3Fifo({ capacity: 2.5 })).toThrow(RangeError);
    expect(() => new S3Fifo({ capacity: 10, smallFraction: 0 })).toThrow(/smallFraction/);
    expect(() => new S3Fifo({ capacity: 10, smallFraction: 1 })).toThrow(/smallFraction/);
  });

  it("explains an evict on an empty cache", () => {
    expect(() => new S3Fifo<string>({ capacity: 4 }).evict()).toThrow(/nothing resident/);
  });

  it("explains a hit for a key it does not hold", () => {
    expect(() => new S3Fifo<string>({ capacity: 4 }).onAccess("ghost", true)).toThrow(
      /does not hold/,
    );
  });

  it("terminates when every main-queue entry is hot", () => {
    // Second chances are spent, not renewed, so a queue of maximally popular
    // entries still yields a victim rather than spinning.
    const capacity = 8;
    const policy = new S3Fifo<number>({ capacity, smallFraction: 0.25 });
    for (let key = 0; key < capacity; key += 1) {
      policy.onAccess(key, false);
      for (let bump = 0; bump < 3; bump += 1) policy.onAccess(key, true);
    }
    policy.onAccess(99, false);
    expect(typeof policy.evict()).toBe("number");
    expect(policy.size()).toBe(capacity);
  });

  it("does not leak ghost identifiers", () => {
    const capacity = 8;
    const policy = new S3Fifo<number>({ capacity });
    for (let key = 0; key < 5_000; key += 1) {
      policy.onAccess(key, false);
      while (policy.size() > capacity) policy.evict();
    }

    let ghosts = 0;
    for (let key = 0; key < 5_000; key += 1) {
      if (policy.queueOf(key) === "ghost") ghosts += 1;
    }
    // The ghost queue holds at most as many keys as the main queue has entries.
    expect(ghosts).toBeLessThanOrEqual(capacity);
  });

  it("keeps a reused key while discarding one-hit wonders", () => {
    // The design in one trace: "hot" is reused inside the small queue and is
    // promoted, while the stream of newcomers passes straight through.
    const capacity = 10;
    const options = { capacity, keyUniverse: 500 };
    const events: number[] = [1, 1, 1];
    for (let key = 100; key < 140; key += 1) events.push(key);
    events.push(1);

    const trace = Uint32Array.from(events);
    const s3 = runCacheTrace(new S3Fifo<number>({ capacity }), trace, options);
    const fifo = runCacheTrace(new Fifo<number>({ capacity }), trace, options);

    // Plain FIFO loses key 1 to the stream; S3-FIFO still has it at the end.
    expect(s3.hits).toBe(3);
    expect(fifo.hits).toBe(2);
  });

  it("beats LRU on the scan-heavy canonical trace", () => {
    const spec = CACHE_TRACES["scan-heavy"]!;
    const trace = generateCacheTrace(spec.id);
    const options = { capacity: spec.capacity, keyUniverse: spec.keyUniverse };

    const s3 = cacheMetrics(runCacheTrace(new S3Fifo<number>({ capacity: spec.capacity }), trace, options));
    const lru = cacheMetrics(runCacheTrace(new Lru<number>({ capacity: spec.capacity }), trace, options));

    expect(s3.hitRate).toBeGreaterThan(lru.hitRate);
  });

  it("beats plain FIFO on the canonical Zipf trace", () => {
    // Two bits and a small queue are what separate this from its baseline.
    const spec = CACHE_TRACES["zipf-1.0-100k"]!;
    const trace = generateCacheTrace(spec.id);
    const options = { capacity: spec.capacity, keyUniverse: spec.keyUniverse };

    const s3 = cacheMetrics(runCacheTrace(new S3Fifo<number>({ capacity: spec.capacity }), trace, options));
    const fifo = cacheMetrics(runCacheTrace(new Fifo<number>({ capacity: spec.capacity }), trace, options));

    expect(s3.hitRate).toBeGreaterThan(fifo.hitRate + 0.05);
  });
});
