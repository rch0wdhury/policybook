/**
 * What the rate-limiter runner shows, and that it keeps showing the same thing.
 *
 * Companion to `viz/cache.test.ts`, for the same reasons: `simulation.test.ts`
 * proves the runner's metrics match the core harness, and these cover the layer
 * the picture is drawn from — the per-key budget gauges, the retry-after hint,
 * the busiest-key tallies — plus committed figures that would move if anything
 * beneath the runner drifted.
 */

import { describe, expect, it } from "vitest";
import {
  RATE_LIMITER_TRACES,
  generateRateLimiterTrace,
} from "../../../../packages/core/src/domains/rate-limiter";
import DualBucket from "../../../../policies/rate-limiter/dual-bucket/index";
import FixedWindow from "../../../../policies/rate-limiter/fixed-window/index";
import Gcra from "../../../../policies/rate-limiter/gcra/index";
import LeakyBucket from "../../../../policies/rate-limiter/leaky-bucket/index";
import SlidingCounter from "../../../../policies/rate-limiter/sliding-counter/index";
import SlidingLog from "../../../../policies/rate-limiter/sliding-log/index";
import TokenBucket from "../../../../policies/rate-limiter/token-bucket/index";
import { createSimulation, type RateLimiterView } from "../lib/simulation";
import { decisionsLayout } from "./rate-limiter";

/** The reference configuration every canonical benchmark uses. */
const PARAMS = { ratePerSec: 100, burst: 100, maxKeys: 1024 };

type Build = (params: Record<string, unknown>) => unknown;
const build =
  (Policy: new (params: typeof PARAMS) => unknown): Build =>
  (params) =>
    new Policy(params as typeof PARAMS);

const TOKEN_BUCKET = build(TokenBucket);
const LEAKY_BUCKET = build(LeakyBucket);
const FIXED_WINDOW = build(FixedWindow);
const GCRA = build(Gcra);
const DUAL_BUCKET = build(DualBucket);
const SLIDING_LOG = build(SlidingLog);
const SLIDING_COUNTER = build(SlidingCounter);

function run(
  policy: Build,
  traceId: string,
): { view: RateLimiterView; metrics: Record<string, number> } {
  const simulation = createSimulation("rate-limiter", policy, PARAMS, traceId);
  simulation.seek(simulation.totalSteps);
  const frame = simulation.frame();
  if (frame.view.kind !== "rate-limiter") throw new Error("expected a rate-limiter view");
  return { view: frame.view, metrics: frame.metrics };
}

describe("what the rate-limiter runner reports", () => {
  /**
   * The committed figures, cross-checked against `runRateLimiterTrace` on the
   * core harness rather than recorded from the runner alone.
   *
   * Both the accept rate *and* the burst are pinned, because on this workload
   * fixed-window and sliding-log admit exactly the same fraction (0.337363) and
   * differ only in the shape of what they admit — 46 in a 100 ms window against
   * 38. An accept rate alone would call those two policies the same.
   */
  it.each([
    ["token-bucket", TOKEN_BUCKET, 0.342929, 36],
    ["leaky-bucket", LEAKY_BUCKET, 0.272589, 10],
    ["fixed-window", FIXED_WINDOW, 0.337363, 46],
    ["gcra", GCRA, 0.342929, 36],
    ["sliding-log", SLIDING_LOG, 0.337363, 38],
  ])("%s holds its committed shape under sustained overload", (_name, policy, rate, burst) => {
    const { metrics } = run(policy, "overload");
    expect(metrics["acceptRate"]).toBe(rate);
    expect(metrics["maxBurst100ms"]).toBe(burst);
  });

  it.each([
    ["token-bucket", TOKEN_BUCKET, 1],
    ["leaky-bucket", LEAKY_BUCKET, 0.184437],
    ["fixed-window", FIXED_WINDOW, 0.969205],
    ["dual-bucket", DUAL_BUCKET, 0.325828],
  ])("%s holds its committed accept rate on the bursty trace", (_name, policy, rate) => {
    expect(run(policy, "bursty").metrics["acceptRate"]).toBe(rate);
  });

  /**
   * A registry fact, pinned deliberately rather than discovered by surprise.
   *
   * GCRA is the virtual-scheduling formulation of a token bucket, so at the
   * same rate and burst the two are the same limiter: they agree on **every
   * arrival of all four traces**, not merely in aggregate. What separates them
   * is bookkeeping — GCRA carries one timestamp per key — and that is a memory
   * story, not a behaviour one.
   *
   * The test exists so that if they ever *stop* agreeing, someone finds out
   * from a failure rather than from a reader.
   */
  it("has GCRA and the token bucket agreeing exactly, as the theory says", () => {
    for (const traceId of Object.keys(RATE_LIMITER_TRACES)) {
      const bucket = run(TOKEN_BUCKET, traceId).metrics;
      const gcra = run(GCRA, traceId).metrics;
      expect(gcra["acceptRate"], traceId).toBe(bucket["acceptRate"]);
      expect(gcra["maxBurst100ms"], traceId).toBe(bucket["maxBurst100ms"]);
    }
  });
});

describe("the per-key budget the picture is drawn from", () => {
  it.each([
    ["token-bucket", TOKEN_BUCKET, ["tokens"]],
    ["leaky-bucket", LEAKY_BUCKET, ["level"]],
    ["fixed-window", FIXED_WINDOW, ["count in window"]],
    ["gcra", GCRA, ["tokens"]],
    ["sliding-log", SLIDING_LOG, ["count in window"]],
    ["sliding-counter", SLIDING_COUNTER, ["weighted estimate"]],
  ])("reads %s's own gauge", (_name, policy, labels) => {
    const { view } = run(policy, "bursty");
    expect(view.gauges.map((gauge) => gauge.label)).toEqual(labels);
    for (const gauge of view.gauges) {
      expect(gauge.values.length).toBe(view.gaugeTimes.length);
      expect(gauge.values.every((value) => Number.isFinite(value))).toBe(true);
    }
  });

  it("shows both of dual-bucket's budgets, not one of them", () => {
    // Two limits interacting is the entire point of the policy; showing one
    // would be a picture of a different, simpler limiter.
    const { view } = run(DUAL_BUCKET, "bursty");
    expect(view.gauges.map((gauge) => gauge.label)).toEqual(["tokens", "requests"]);
  });

  it("keeps the gauge samples aligned with their timestamps", () => {
    const { view } = run(TOKEN_BUCKET, "bursty");
    expect(view.gaugeTimes.length).toBeGreaterThan(0);
    // Time never runs backwards, which is what a mis-trimmed ring buffer does.
    for (let index = 1; index < view.gaugeTimes.length; index += 1) {
      expect(view.gaugeTimes[index]!).toBeGreaterThanOrEqual(view.gaugeTimes[index - 1]!);
    }
  });

  it("follows one fixed key on a multi-key trace, not whoever just arrived", () => {
    /*
     * The bug this exists for. The gauge was originally sampled for the key of
     * the *current arrival*, which on a 10,000-key trace plots one point from
     * each of ten thousand different buckets and calls it a budget over time.
     *
     * The check has to be an exact one. A shape heuristic — "consecutive
     * samples move by less than the burst" — passes under the bug, because
     * every key on this trace sits near its full allowance and a scatter of
     * unrelated near-full buckets looks much like one near-full bucket. So the
     * expected series is computed here independently, by replaying the trace
     * and asking the policy about the one key throughout.
     */
    const { view } = run(TOKEN_BUCKET, "many-keys");
    expect(view.gaugeKey).toBe(view.topKeys[0]!.key);

    const trace = generateRateLimiterTrace("many-keys", 20_000);
    const policy = new TokenBucket(PARAMS) as TokenBucket & {
      tokensOf(key: number, now: number): number;
    };
    const expected: number[] = [];
    for (let index = 0; index < trace.times.length; index += 1) {
      policy.allow(trace.keys[index]!, 1, trace.times[index]!);
      expected.push(policy.tokensOf(view.gaugeKey, trace.times[index]!));
    }

    const values = view.gauges[0]!.values;
    expect(values.length).toBeGreaterThan(2);
    expect(values).toEqual(expected.slice(-values.length));
  });

  it("names the single key of a single-key trace", () => {
    expect(run(TOKEN_BUCKET, "bursty").view.gaugeKey).toBe(0);
  });

  it("reports a retry-after hint where the policy offers one", () => {
    const { view } = run(LEAKY_BUCKET, "bursty");
    expect(view.retryAfter).not.toBeNull();
    expect(view.retryAfter!).toBeGreaterThanOrEqual(0);
  });
});

describe("the busiest keys", () => {
  it("says nothing for a single-key trace", () => {
    expect(run(TOKEN_BUCKET, "bursty").view.topKeys).toEqual([]);
  });

  it("follows the eight busiest keys of a multi-key trace", () => {
    const { view } = run(LEAKY_BUCKET, "many-keys");
    expect(view.topKeys).toHaveLength(8);

    for (const entry of view.topKeys) {
      expect(entry.arrivals).toBeGreaterThan(0);
      // A limiter cannot grant what was never asked for.
      expect(entry.accepted).toBeLessThanOrEqual(entry.arrivals);
    }

    // Busiest first, so the picture does not reshuffle as the reader scrubs.
    for (let index = 1; index < view.topKeys.length; index += 1) {
      expect(view.topKeys[index]!.arrivals).toBeLessThanOrEqual(
        view.topKeys[index - 1]!.arrivals,
      );
    }
  });

  it("ranks by whole-trace traffic, not by traffic so far", () => {
    // The ranking is computed once from the full trace. Were it computed from
    // what has been played, the bars would reorder under the reader's cursor.
    const early = createSimulation("rate-limiter", LEAKY_BUCKET, PARAMS, "many-keys");
    early.seek(500);
    const earlyFrame = early.frame();
    if (earlyFrame.view.kind !== "rate-limiter") throw new Error("expected a rate-limiter view");

    const late = run(LEAKY_BUCKET, "many-keys").view;
    expect(earlyFrame.view.topKeys.map((entry) => entry.key)).toEqual(
      late.topKeys.map((entry) => entry.key),
    );
  });
});

describe("the decisions strip's layout", () => {
  // The ring buffer holds 240 decisions and the strip claims to show "the
  // last N arrivals". The one unforgivable outcome is dropping the *newest*
  // marks — the decisions arriving as the reader watches — which is what the
  // old layout did by flooring the slot at 3px and clipping on the right.

  it("shows every mark when the canvas is wide enough", () => {
    const { slot, first } = decisionsLayout(1200, 240);
    expect(first).toBe(0);
    expect(slot).toBe(5);
  });

  it("shrinks slots to a pixel before dropping anything", () => {
    // 240 marks at 1px each still fit a 240px canvas whole.
    const { slot, first } = decisionsLayout(240, 240);
    expect(slot).toBe(1);
    expect(first).toBe(0);
  });

  it("drops the oldest marks, never the newest, on a narrow canvas", () => {
    const { slot, first } = decisionsLayout(120, 240);
    expect(slot).toBe(1);
    // Drawing starts partway in: the marks before `first` are the oldest, and
    // the run through the end of the buffer is the newest 120.
    expect(first).toBe(120);
  });

  it("never lays a mark past the right edge", () => {
    for (const width of [1, 7, 50, 119, 240, 999]) {
      for (const count of [1, 8, 239, 240]) {
        const { slot, first } = decisionsLayout(width, count);
        const drawn = count - first;
        expect(drawn).toBeGreaterThan(0);
        // The last mark's slot must end inside the canvas — or be the single
        // mark a sub-slot-width canvas still shows rather than going blank.
        if (drawn > 1) expect(drawn * slot).toBeLessThanOrEqual(width);
      }
    }
  });

  it("caps the slot so few marks do not sprawl", () => {
    expect(decisionsLayout(1200, 10).slot).toBe(9);
  });
});
