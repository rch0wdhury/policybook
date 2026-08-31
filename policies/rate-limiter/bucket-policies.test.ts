/**
 * The token bucket, the leaky bucket and GCRA are one algorithm.
 *
 * Each policy's own suite checks that policy. This file checks the claim the
 * three READMEs make *about each other*: that they are the same rule written
 * three ways. The leaky bucket is the token bucket under
 * `tokens = capacity - level`; GCRA is the token bucket with the balance and
 * the carry folded into a single scheduled instant.
 *
 * It is worth checking rather than asserting, because it is the kind of claim
 * that is true when written and quietly stops being true when someone changes
 * how one of them handles saturation. It also earns the registry the right to
 * say the useful thing out loud — that these three names are one idea, and the
 * choice between them is about state size and vocabulary.
 */

import { describe, expect, it } from "vitest";
import { Rng } from "../../packages/core/src/rng";
import Gcra from "./gcra/index";
import LeakyBucket from "./leaky-bucket/index";
import TokenBucket from "./token-bucket/index";

describe("the token bucket and the leaky bucket are duals", () => {
  it("make identical decisions at equal parameters", () => {
    const rng = new Rng(21);
    let decisions = 0;
    let denials = 0;

    for (let trial = 0; trial < 200; trial += 1) {
      const ratePerSec = 1 + rng.nextInt(500);
      const size = 1 + rng.nextInt(20);
      const tokens = new TokenBucket({ ratePerSec, burst: size });
      const level = new LeakyBucket({ ratePerSec, capacity: size });

      let now = 0;
      for (let step = 0; step < 100; step += 1) {
        now += rng.nextInt(40);
        const cost = 1 + rng.nextInt(3);

        const admittedByTokens = tokens.allow(1, cost, now);
        const admittedByLevel = level.allow(1, cost, now);
        decisions += 1;
        if (!admittedByTokens) denials += 1;

        expect(
          admittedByLevel,
          `disagreed at ${now}ms (rate ${ratePerSec}, size ${size}, cost ${cost})`,
        ).toBe(admittedByTokens);

        // The observable state maps too, not just the yes or no.
        expect(tokens.tokensOf(1, now)).toBe(size - level.levelOf(1, now));
        expect(tokens.retryAfter(1, now)).toBe(level.retryAfter(1, now));
      }
    }

    expect(decisions).toBe(20_000);
    // Guards the guard: two limiters that admitted everything would agree
    // trivially.
    expect(denials).toBeGreaterThan(1_000);
  });

  it("agree with GCRA, which keeps the same information in one integer", () => {
    // GCRA stores neither a balance nor a carry — only the instant the next
    // request is due — and still reaches the same decisions, the same
    // `retryAfter`, and the same visible token count. That is the claim its
    // README makes and the reason to prefer it: identical behaviour, a third
    // of the state.
    const rng = new Rng(31);
    let denials = 0;

    for (let trial = 0; trial < 200; trial += 1) {
      const ratePerSec = 1 + rng.nextInt(500);
      const burst = 1 + rng.nextInt(20);
      const tokens = new TokenBucket({ ratePerSec, burst });
      const gcra = new Gcra({ ratePerSec, burst });

      let now = 0;
      for (let step = 0; step < 100; step += 1) {
        now += rng.nextInt(40);
        const cost = 1 + rng.nextInt(3);

        const byTokens = tokens.allow(1, cost, now);
        const byGcra = gcra.allow(1, cost, now);
        if (!byTokens) denials += 1;

        expect(
          byGcra,
          `disagreed at ${now}ms (rate ${ratePerSec}, burst ${burst}, cost ${cost})`,
        ).toBe(byTokens);
        expect(gcra.tokensOf(1, now)).toBe(tokens.tokensOf(1, now));
        expect(gcra.retryAfter(1, now)).toBe(tokens.retryAfter(1, now));
      }
    }

    expect(denials).toBeGreaterThan(1_000);
  });

  it("agree on the fractional carry, where a float implementation would drift", () => {
    // Three units a second divides no whole number of milliseconds. Both
    // policies carry the remainder in thousandths, so both cross the boundary
    // on the same millisecond — 334, not 333.
    const tokens = new TokenBucket({ ratePerSec: 3, burst: 2 });
    const level = new LeakyBucket({ ratePerSec: 3, capacity: 2 });
    tokens.allow(1, 2, 0);
    level.allow(1, 2, 0);

    expect(tokens.tokensOf(1, 333)).toBe(0);
    expect(level.levelOf(1, 333)).toBe(2);
    expect(tokens.tokensOf(1, 334)).toBe(1);
    expect(level.levelOf(1, 334)).toBe(1);
  });
});

describe("their defaults are what actually differ", () => {
  it("lets a token bucket spend a full burst at once", () => {
    const limiter = new TokenBucket();
    let admitted = 0;
    for (let i = 0; i < 200; i += 1) if (limiter.allow(1, 1, 0)) admitted += 1;
    expect(admitted).toBe(100);
  });

  it("holds a leaky bucket to one request at a time", () => {
    // The same instant, the same rate, and the default capacity of 1: a leaky
    // bucket admits exactly one. That is the smoothing it is chosen for, and it
    // is a difference of configuration rather than of algorithm.
    const limiter = new LeakyBucket();
    let admitted = 0;
    for (let i = 0; i < 200; i += 1) if (limiter.allow(1, 1, 0)) admitted += 1;
    expect(admitted).toBe(1);
  });

  it("spaces a leaky bucket's admissions evenly at its default", () => {
    const limiter = new LeakyBucket();
    const admitted: number[] = [];
    for (let now = 0; now < 200; now += 1) if (limiter.allow(1, 1, now)) admitted.push(now);

    // 100 a second is one every 10 ms, from the first request onward.
    expect(admitted).toEqual([0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160, 170, 180, 190]);
  });

  it("holds both to the same long-run rate despite that", () => {
    // Over a minute of continuous pressure the burst allowance is a rounding
    // error: what a limiter permits in the long run is its rate, and these two
    // agree on it exactly.
    const tokens = new TokenBucket();
    const level = new LeakyBucket();
    let byTokens = 0;
    let byLevel = 0;
    for (let now = 0; now < 60_000; now += 1) {
      if (tokens.allow(1, 1, now)) byTokens += 1;
      if (level.allow(1, 1, now)) byLevel += 1;
    }
    // The last request is at 59,999 ms, so 59,999 ms of refill elapse rather
    // than a full minute: 100 x 59,999 / 1,000 = 5,999 tokens accrued, plus the
    // 100 the bucket started with. The leaky bucket has no starting allowance
    // beyond its capacity of one, so it admits at 0, 10, 20 … 59,990.
    expect(byTokens).toBe(6_099);
    expect(byLevel).toBe(6_000);
    // Within a sixth of a percent of each other, and both within a sixth of a
    // percent of the configured 100/s — which is the point.
    expect(Math.abs(byTokens - byLevel) / byLevel).toBeLessThan(0.02);
  });
});

describe("shared contract", () => {
  const factories = {
    "token-bucket": (rate: number, size: number) =>
      new TokenBucket({ ratePerSec: rate, burst: size }),
    "leaky-bucket": (rate: number, size: number) =>
      new LeakyBucket({ ratePerSec: rate, capacity: size }),
  };
  const names = Object.keys(factories) as (keyof typeof factories)[];

  it.each(names)("%s rejects parameters that are not positive integers", (name) => {
    const make = factories[name];
    expect(() => make(0, 5)).toThrow(RangeError);
    expect(() => make(1.5, 5)).toThrow(RangeError);
    expect(() => make(100, 0)).toThrow(RangeError);
    expect(() => make(100, -1)).toThrow(RangeError);
  });

  it.each(names)("%s reports a wait that admits when it elapses", (name) => {
    const rng = new Rng(9);
    let denials = 0;

    for (let trial = 0; trial < 100; trial += 1) {
      const limiter = factories[name](1 + rng.nextInt(300), 1 + rng.nextInt(8));
      let now = 0;
      for (let step = 0; step < 60; step += 1) {
        now += rng.nextInt(30);
        if (limiter.allow(1, 1, now)) continue;
        denials += 1;
        const wait = limiter.retryAfter(1, now);
        expect(limiter.allow(1, 1, now + wait), `${name}: refused after waiting ${wait}ms`).toBe(
          true,
        );
        now += wait;
      }
    }
    expect(denials).toBeGreaterThan(100);
  });

  it.each(names)("%s tracks keys independently", (name) => {
    const limiter = factories[name](100, 2);
    expect(limiter.allow(1, 2, 0)).toBe(true);
    expect(limiter.allow(1, 1, 0)).toBe(false);
    expect(limiter.allow(2, 2, 0)).toBe(true);
    expect(limiter.stateSize()).toBe(2);
  });
});
