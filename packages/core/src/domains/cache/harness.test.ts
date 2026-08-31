import { describe, expect, it } from "vitest";
import { runCacheTrace } from "./harness";
import type { CachePolicy } from "./interface";
import { cacheMetrics } from "./metrics";

/** A scripted FIFO, so the harness's behaviour is the only thing under test. */
class ScriptedFifo implements CachePolicy<number> {
  readonly accesses: { key: number; hit: boolean }[] = [];
  readonly order: number[] = [];

  onAccess(key: number, hit: boolean): void {
    this.accesses.push({ key, hit });
    if (!hit) this.order.push(key);
  }

  evict(): number {
    const victim = this.order.shift();
    if (victim === undefined) throw new Error("evict called with nothing resident");
    return victim;
  }
}

/** Declines every key whose value is odd. */
class PickyFifo extends ScriptedFifo {
  admit(key: number): boolean {
    return key % 2 === 0;
  }

  override onAccess(key: number, hit: boolean): void {
    this.accesses.push({ key, hit });
    // Only track keys that will actually be admitted.
    if (!hit && key % 2 === 0) this.order.push(key);
  }
}

const options = { capacity: 2, keyUniverse: 16 };

describe("runCacheTrace", () => {
  it("counts hits and misses", () => {
    const policy = new ScriptedFifo();
    const result = runCacheTrace(policy, Uint32Array.from([1, 2, 1, 3, 1]), options);

    // 1 miss, 2 miss, 1 hit, 3 miss and evicts 1 (FIFO ignores the hit), 1 miss
    // again. That last miss is FIFO's whole weakness in one line: it threw away
    // the key it had just served.
    expect(result.events).toBe(5);
    expect(result.misses).toBe(4);
    expect(result.hits).toBe(1);
  });

  it("tells the policy whether each access hit, before inserting", () => {
    const policy = new ScriptedFifo();
    runCacheTrace(policy, Uint32Array.from([1, 1, 2]), options);

    // The first access to a key is always a miss: onAccess is called before
    // insertion, so a policy never sees its own pending insert as a hit.
    expect(policy.accesses).toEqual([
      { key: 1, hit: false },
      { key: 1, hit: true },
      { key: 2, hit: false },
    ]);
  });

  it("evicts only once capacity is exceeded", () => {
    const policy = new ScriptedFifo();
    const result = runCacheTrace(policy, Uint32Array.from([1, 2]), options);
    expect(result.evictions).toBe(0);

    const overflowing = new ScriptedFifo();
    const third = runCacheTrace(overflowing, Uint32Array.from([1, 2, 3]), options);
    expect(third.evictions).toBe(1);
  });

  it("evicts in the policy's chosen order", () => {
    const policy = new ScriptedFifo();
    runCacheTrace(policy, Uint32Array.from([1, 2, 3, 1]), options);
    // FIFO evicted 1 to admit 3, so the later access to 1 is a miss.
    expect(policy.accesses.at(-1)).toEqual({ key: 1, hit: false });
  });

  it("honours admission control", () => {
    const policy = new PickyFifo();
    const result = runCacheTrace(policy, Uint32Array.from([2, 3, 4, 3]), options);

    // 3 is refused both times, so it is never resident and never a hit.
    expect(result.admissionRejections).toBe(2);
    expect(result.hits).toBe(0);
    // Only 2 and 4 were admitted, which fits in capacity 2.
    expect(result.evictions).toBe(0);
  });

  it("rejects a policy that evicts something it does not hold", () => {
    const rogue: CachePolicy<number> = {
      onAccess(): void {},
      evict(): number {
        return 9; // never inserted
      },
    };
    expect(() => runCacheTrace(rogue, Uint32Array.from([1, 2, 3]), options)).toThrow(
      /evicted key 9 .* does not hold/,
    );
  });

  it("rejects a trace key outside the key universe", () => {
    const policy = new ScriptedFifo();
    expect(() => runCacheTrace(policy, Uint32Array.from([99]), options)).toThrow(
      /outside the key universe/,
    );
  });

  it("rejects nonsense options", () => {
    const policy = new ScriptedFifo();
    const trace = Uint32Array.from([1]);
    expect(() => runCacheTrace(policy, trace, { capacity: 0, keyUniverse: 4 })).toThrow(RangeError);
    expect(() => runCacheTrace(policy, trace, { capacity: 2, keyUniverse: 0 })).toThrow(RangeError);
  });

  it("keeps the cache full once warmed", () => {
    // Every miss after the cache fills must free exactly one slot, so the
    // eviction count is pinned by the miss count.
    const policy = new ScriptedFifo();
    const trace = Uint32Array.from([1, 2, 3, 4, 5, 6]);
    const result = runCacheTrace(policy, trace, options);
    expect(result.misses).toBe(6);
    expect(result.evictions).toBe(result.misses - options.capacity);
  });
});

describe("cacheMetrics", () => {
  it("reports hit rate and evictions", () => {
    const policy = new ScriptedFifo();
    const result = runCacheTrace(policy, Uint32Array.from([1, 1, 1, 2]), options);
    expect(cacheMetrics(result)).toEqual({ hitRate: 0.5, evictions: 0 });
  });

  it("rounds the hit rate to six places", () => {
    const policy = new ScriptedFifo();
    // 1 hit in 3 events is 0.3333...
    const result = runCacheTrace(policy, Uint32Array.from([1, 1, 2]), options);
    expect(cacheMetrics(result).hitRate).toBe(0.333333);
  });

  it("reports zero for an empty trace", () => {
    const policy = new ScriptedFifo();
    const result = runCacheTrace(policy, new Uint32Array(0), options);
    expect(cacheMetrics(result)).toEqual({ hitRate: 0, evictions: 0 });
  });
});
