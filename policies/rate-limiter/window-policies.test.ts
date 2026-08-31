/**
 * What separates the three window limiters from each other.
 *
 * Each policy's own suite checks that policy. This one checks the claims the
 * READMEs make *about the comparison* — the boundary burst, and the promise
 * that `retryAfter` is worth acting on. Those are properties of the set, and a
 * change that broke one of them would otherwise pass three green suites.
 */

import { describe, expect, it } from "vitest";
import { Rng } from "../../packages/core/src/rng";
import FixedWindow from "./fixed-window/index";
import SlidingCounter from "./sliding-counter/index";
import SlidingLog from "./sliding-log/index";

interface Limiter {
  allow(key: number, cost: number, now: number): boolean;
  retryAfter(key: number, now: number): number;
  stateSize(): number;
}

type Params = { limit: number; windowMs: number };

const FACTORIES: Record<string, (params: Params) => Limiter> = {
  "fixed-window": (params) => new FixedWindow(params),
  "sliding-log": (params) => new SlidingLog(params),
  "sliding-counter": (params) => new SlidingCounter(params),
};

const NAMES = Object.keys(FACTORIES);

describe("the boundary burst", () => {
  /** `limit` requests just before a window edge, then `limit` just after. */
  function burstAcrossBoundary(limiter: Limiter, limit: number, windowMs: number): number {
    let admitted = 0;
    for (let i = 0; i < limit; i += 1) if (limiter.allow(1, 1, windowMs - 1)) admitted += 1;
    for (let i = 0; i < limit; i += 1) if (limiter.allow(1, 1, windowMs)) admitted += 1;
    return admitted;
  }

  it("lets twice the limit through a fixed window in two milliseconds", () => {
    // The flaw that motivates both of the others. This is not a rounding
    // artefact: the downstream service really does see 2 x limit.
    const admitted = burstAcrossBoundary(new FixedWindow({ limit: 5, windowMs: 1_000 }), 5, 1_000);
    expect(admitted).toBe(10);
  });

  it.each(["sliding-log", "sliding-counter"])("is refused by %s", (name) => {
    const admitted = burstAcrossBoundary(FACTORIES[name]!({ limit: 5, windowMs: 1_000 }), 5, 1_000);
    expect(admitted).toBe(5);
  });

  it("releases the budget continuously under sliding-counter, all at once under sliding-log", () => {
    // Halfway through the window after a burst, the counter has faded 2 of the
    // 5 back in while the log still holds every one of them until they expire
    // together. That difference — smooth versus stepped — is the reason the
    // counter is the usual production choice despite being an estimate.
    const log = new SlidingLog({ limit: 5, windowMs: 1_000 });
    const counter = new SlidingCounter({ limit: 5, windowMs: 1_000 });
    for (const limiter of [log, counter]) limiter.allow(1, 5, 999);

    expect(counter.allow(1, 3, 1_500)).toBe(true);
    expect(log.allow(1, 1, 1_500)).toBe(false);
    // The log's five leave together at 1,999, and not before.
    expect(log.allow(1, 5, 1_999)).toBe(true);
  });
});

describe("retryAfter is honest", () => {
  // A hint nobody can act on is worse than none. With a single key and no
  // competing traffic, waiting exactly `retryAfter` must admit — there is no
  // other request in flight that could take the slot.
  //
  // This property found a real bug in sliding-counter: it returned the window
  // edge, where the current count becomes the previous count and, undecayed,
  // still refuses. Four denials in five reported a wait that did not work.
  it.each(NAMES)("%s admits after waiting exactly as long as it asked", (name) => {
    const rng = new Rng(7);
    let denials = 0;

    for (let trial = 0; trial < 200; trial += 1) {
      const limit = 1 + rng.nextInt(6);
      const windowMs = 10 + rng.nextInt(200);
      const limiter = FACTORIES[name]!({ limit, windowMs });
      let now = 0;

      for (let step = 0; step < 40; step += 1) {
        now += rng.nextInt(windowMs);
        if (limiter.allow(1, 1, now)) continue;

        denials += 1;
        const wait = limiter.retryAfter(1, now);
        expect(wait).toBeGreaterThanOrEqual(0);
        expect(
          limiter.allow(1, 1, now + wait),
          `${name}: refused after waiting ${wait}ms at ${now} (limit ${limit}, window ${windowMs})`,
        ).toBe(true);
        now += wait;
      }
    }

    // Guards the guard: a configuration that never denied would pass vacuously.
    expect(denials).toBeGreaterThan(100);
  });

  it.each(NAMES)("%s asks for no wait when it has capacity", (name) => {
    const limiter = FACTORIES[name]!({ limit: 5, windowMs: 1_000 });
    expect(limiter.retryAfter(1, 0)).toBe(0);
    limiter.allow(1, 1, 0);
    expect(limiter.retryAfter(1, 0)).toBe(0);
  });
});

describe("shared contract", () => {
  it.each(NAMES)("%s rejects parameters that are not positive integers", (name) => {
    const make = FACTORIES[name]!;
    expect(() => make({ limit: 0, windowMs: 1_000 })).toThrow(RangeError);
    expect(() => make({ limit: 1.5, windowMs: 1_000 })).toThrow(RangeError);
    expect(() => make({ limit: 5, windowMs: 0 })).toThrow(RangeError);
    expect(() => make({ limit: 5, windowMs: -1 })).toThrow(RangeError);
  });

  it.each(NAMES)("%s never admits more than the limit inside one window", (name) => {
    // The weakest claim all three actually make. A sliding log holds it over
    // every interval; the other two hold it over a window they align to
    // themselves, which is what the boundary tests above are about.
    const limiter = FACTORIES[name]!({ limit: 5, windowMs: 1_000 });
    let admitted = 0;
    for (let now = 0; now < 1_000; now += 1) if (limiter.allow(1, 1, now)) admitted += 1;
    expect(admitted).toBe(5);
  });

  it.each(NAMES)("%s tracks keys independently", (name) => {
    const limiter = FACTORIES[name]!({ limit: 2, windowMs: 1_000 });
    expect(limiter.allow(1, 2, 0)).toBe(true);
    expect(limiter.allow(1, 1, 0)).toBe(false);
    expect(limiter.allow(2, 2, 0)).toBe(true);
    expect(limiter.stateSize()).toBe(2);
  });

  it.each(NAMES)("%s refuses a cost larger than the limit rather than wedging", (name) => {
    const limiter = FACTORIES[name]!({ limit: 5, windowMs: 1_000 });
    expect(limiter.allow(1, 6, 0)).toBe(false);
    // And is otherwise unaffected: the refused request consumed nothing.
    expect(limiter.allow(1, 5, 0)).toBe(true);
  });
});
