/**
 * What the cache runner shows, and that it keeps showing the same thing.
 *
 * The parity tests in `lib/simulation.test.ts` prove the runner's *metrics*
 * match the core harness. These cover the layer above: the per-entry state the
 * picture is drawn from, the prefix of each trace the runner replays, and
 * committed hit rates that would move if anything underneath drifted.
 *
 * Drawing itself is not tested — asserting on canvas pixels is expensive and
 * tells you almost nothing — so `cache.ts` is kept to functions that take a
 * canvas and a view, and everything that decides *what* is drawn lives here.
 */

import { describe, expect, it } from "vitest";
import {
  CACHE_TRACES,
  SCAN_INTERVAL,
  SHIFT_INTERVAL,
} from "../../../../packages/core/src/domains/cache";
import Clock from "../../../../policies/cache/clock/index";
import Lfu from "../../../../policies/cache/lfu/index";
import Lru from "../../../../policies/cache/lru/index";
import Sieve from "../../../../policies/cache/sieve/index";
import { createSimulation, reducedLength, type CacheView } from "../lib/simulation";

type Build = (params: Record<string, unknown>) => unknown;

const SIEVE: Build = (p) => new Sieve(p as { capacity: number });
const LRU: Build = (p) => new Lru(p as { capacity: number });
const CLOCK: Build = (p) => new Clock(p as { capacity: number });
const LFU: Build = (p) => new Lfu(p as { capacity: number });

/** Run a policy over a whole trace exactly as a policy page would. */
function run(build: Build, traceId = "zipf-1.0-100k"): { view: CacheView; hitRate: number } {
  const spec = CACHE_TRACES[traceId]!;
  const simulation = createSimulation("cache", build, { capacity: spec.capacity }, traceId);
  simulation.seek(simulation.totalSteps);
  const frame = simulation.frame();
  if (frame.view.kind !== "cache") throw new Error("expected a cache view");
  return { view: frame.view, hitRate: frame.metrics["hitRate"]! };
}

describe("the slice of each trace the runner replays", () => {
  /**
   * The regression this exists for.
   *
   * The runner used to replay a flat 20,000 events of every trace. Two of the
   * four cache traces do something on a schedule — scan-heavy injects a scan
   * every 20,000 accesses, shifting-popularity rotates its hot set every 25,000
   * — so a 20,000-event prefix of either contained none of it. The runner
   * replayed plain Zipf under a name promising otherwise, beside a benchmark
   * table reporting the real trace.
   */
  it.each([
    ["scan-heavy", SCAN_INTERVAL],
    ["shifting-popularity", SHIFT_INTERVAL],
  ])("replays %s long enough for its behaviour to happen twice", (traceId, period) => {
    expect(reducedLength("cache", traceId)).toBeGreaterThan(period * 2);
  });

  it("never asks for more of a trace than exists", () => {
    for (const [traceId, spec] of Object.entries(CACHE_TRACES)) {
      expect(reducedLength("cache", traceId)).toBeLessThanOrEqual(spec.events);
    }
  });

  it("leaves an unscheduled trace at the default", () => {
    expect(reducedLength("cache", "zipf-1.0-100k")).toBe(20_000);
  });
});

describe("what the cache runner reports", () => {
  /**
   * The committed figures, cross-checked against `runCacheTrace` on the core
   * harness rather than recorded from the runner alone — a number the runner
   * merely agrees with itself about would guard nothing.
   *
   * If a policy, a trace generator, the stepper, or the replayed prefix
   * changes, one of these moves. Moving them should be a deliberate act with a
   * reason.
   */
  it.each([
    ["SIEVE", SIEVE, 0.70005],
    ["LRU", LRU, 0.6714],
    ["CLOCK", CLOCK, 0.68005],
    ["LFU", LFU, 0.70005],
  ])("%s reaches its committed hit rate on the everyday trace", (_name, build, expected) => {
    expect(run(build).hitRate).toBe(expected);
  });

  /**
   * The same, on the trace that separates the policies.
   *
   * SIEVE and LFU tie *exactly* on stationary Zipf — same hit count, same
   * eviction count, same victims in the same order — because the cold tail is
   * accessed once per key, so LFU chooses among an all-zero-frequency set in
   * insertion order and SIEVE scans its all-unvisited cold entries in the same
   * order. That is real, and confirmed against the committed benchmarks; it is
   * also why a committed figure on that trace alone could not catch one policy
   * collapsing into another. Here they are 0.13 apart.
   */
  it.each([
    ["SIEVE", SIEVE, 0.630128],
    ["LRU", LRU, 0.667024],
    ["CLOCK", CLOCK, 0.675072],
    ["LFU", LFU, 0.499296],
  ])("%s reaches its committed hit rate once popularity shifts", (_name, build, expected) => {
    expect(run(build, "shifting-popularity").hitRate).toBe(expected);
  });

  it("tells the policies apart on a trace that has shifted", () => {
    // Guards the regression from the other side: were the prefix cut short
    // again, these would collapse back onto one number.
    expect(run(SIEVE, "shifting-popularity").hitRate).not.toBe(
      run(LFU, "shifting-popularity").hitRate,
    );
  });

  it("fills the cache to capacity and no further", () => {
    const { view } = run(SIEVE);
    expect(view.resident.length).toBe(1_000);
    expect(view.capacity).toBe(1_000);
  });

  it("samples a hit-rate history the chart can draw", () => {
    const { view, hitRate } = run(SIEVE);
    expect(view.history.length).toBe(20_000 / 50);
    for (const value of view.history) {
      expect(value).toBeGreaterThanOrEqual(0);
      expect(value).toBeLessThanOrEqual(1);
    }
    // The last sample is the final hit rate, to rounding.
    expect(view.history.at(-1)).toBeCloseTo(hitRate, 3);
  });
});

describe("the per-entry state the picture is drawn from", () => {
  it("reads SIEVE's visited bit", () => {
    // The bit that decides SIEVE's next eviction. Showing the real one is the
    // whole point of the grid; a decorative square would be worse than none.
    const { view } = run(SIEVE);
    expect(view.annotationLabel).toBe("visited bit");
    expect(view.annotations).toHaveLength(view.resident.length);
    expect(new Set(view.annotations)).toEqual(new Set(["0", "1"]));
  });

  it("reads CLOCK's reference bit", () => {
    const { view } = run(CLOCK);
    expect(view.annotationLabel).toBe("reference bit");
    expect(new Set(view.annotations)).toEqual(new Set(["0", "1"]));
  });

  it("reads LFU's frequency counter", () => {
    const { view } = run(LFU);
    expect(view.annotationLabel).toBe("frequency");
    for (const value of view.annotations) {
      expect(typeof value).toBe("number");
      expect(value as number).toBeGreaterThanOrEqual(0);
    }
  });

  it("says nothing rather than inventing something, for a policy with no bit", () => {
    // LRU's order *is* its state; there is no per-entry flag to show, and the
    // grid renders plain cells rather than a fabricated one.
    const { view } = run(LRU);
    expect(view.annotationLabel).toBe("");
    expect(view.annotations.every((value) => value === null)).toBe(true);
  });
});
