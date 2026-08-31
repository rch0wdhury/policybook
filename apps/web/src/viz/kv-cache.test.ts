/**
 * What the KV-cache runner shows, and that it keeps showing the same thing.
 *
 * The third of the three viz test files, for the reasons given in
 * `viz/cache.test.ts`: `simulation.test.ts` proves the metrics match the core
 * harness, and these cover what the picture is drawn from — which positions
 * survive, the attention over them, the retained-mass history — plus committed
 * figures that would move if anything beneath the runner drifted.
 */

import { describe, expect, it } from "vitest";
import { KV_CACHE_TRACES } from "../../../../packages/core/src/domains/kv-cache";
import H2o from "../../../../policies/kv-cache/h2o/index";
import Scissorhands from "../../../../policies/kv-cache/scissorhands/index";
import SlidingWindow from "../../../../policies/kv-cache/sliding-window/index";
import SnapKv from "../../../../policies/kv-cache/snapkv/index";
import StreamingLlm from "../../../../policies/kv-cache/streaming-llm/index";
import Tova from "../../../../policies/kv-cache/tova/index";
import { createSimulation, type KvCacheView } from "../lib/simulation";

const BUDGET = 512;
const TRACE = "decode-4096";

type Build = (params: Record<string, unknown>) => unknown;
const build =
  (Policy: new (params: { budget: number }) => unknown): Build =>
  (params) =>
    new Policy(params as { budget: number });

const H2O = build(H2o);
const STREAMING_LLM = build(StreamingLlm);
const SLIDING_WINDOW = build(SlidingWindow);
const SNAPKV = build(SnapKv);
const TOVA = build(Tova);
const SCISSORHANDS = build(Scissorhands);

function run(policy: Build, steps?: number): { view: KvCacheView; metrics: Record<string, number> } {
  const simulation = createSimulation("kv-cache", policy, { budget: BUDGET }, TRACE);
  simulation.seek(steps ?? simulation.totalSteps);
  const frame = simulation.frame();
  if (frame.view.kind !== "kv-cache") throw new Error("expected a kv-cache view");
  return { view: frame.view, metrics: frame.metrics };
}

describe("what the kv-cache runner reports", () => {
  /**
   * The committed figures, cross-checked against `runKvCacheTrace` on the core
   * harness rather than recorded from the runner alone.
   *
   * Both metrics are pinned. They do not rank the policies the same way —
   * TOVA leads on retained mass while H2O leads on heavy-hitter recall — and a
   * policy can hold most of the attention while missing the positions that
   * mattered most, which is exactly the distinction the domain exists to make.
   */
  it.each([
    ["H2O", H2O, 0.714859, 0.913715],
    ["StreamingLLM", STREAMING_LLM, 0.786153, 0.88348],
    ["sliding window", SLIDING_WINDOW, 0.655518, 0.774107],
    ["SnapKV", SNAPKV, 0.82119, 0.901949],
    ["TOVA", TOVA, 0.824012, 0.909793],
    ["Scissorhands", SCISSORHANDS, 0.711377, 0.906047],
  ])("%s holds its committed mass and recall", (_name, policy, mass, recall) => {
    const { metrics } = run(policy);
    expect(metrics["retainedAttentionMass"]).toBe(mass);
    expect(metrics["heavyHitterRecall"]).toBe(recall);
  });

  it("keeps every policy inside its budget", () => {
    for (const policy of [H2O, STREAMING_LLM, SLIDING_WINDOW, SNAPKV, TOVA, SCISSORHANDS]) {
      const { view } = run(policy);
      expect(view.kept.length).toBeLessThanOrEqual(BUDGET);
    }
  });

  it("samples a retained-mass history the chart can draw", () => {
    const { view, metrics } = run(H2O);
    expect(view.history.length).toBeGreaterThan(100);
    for (const value of view.history) {
      expect(value).toBeGreaterThanOrEqual(0);
      // A share of attention, so bounded by 1 — but it is a running mean of
      // float32 softmax rows, each summing to 1 only to float32 precision, so
      // a few parts in a billion above is arithmetic rather than a defect.
      expect(value).toBeLessThanOrEqual(1 + 1e-6);
    }
    // The last sample is the running mass, which is the metric to rounding.
    expect(view.history.at(-1)).toBeCloseTo(metrics["retainedAttentionMass"]!, 2);
  });

  /**
   * The sliding window is the baseline the picture draws dashed, so it had
   * better actually be the thing worth beating — and it is: it retains the
   * least mass of the six. A baseline that led the field would be telling
   * readers the opposite of the truth.
   */
  it("has the sliding-window baseline retaining the least of any policy", () => {
    const baseline = run(SLIDING_WINDOW).metrics["retainedAttentionMass"]!;
    for (const [name, policy] of [
      ["H2O", H2O],
      ["StreamingLLM", STREAMING_LLM],
      ["SnapKV", SNAPKV],
      ["TOVA", TOVA],
      ["Scissorhands", SCISSORHANDS],
    ] as const) {
      expect(run(policy).metrics["retainedAttentionMass"]!, name).toBeGreaterThan(baseline);
    }
  });
});

/** How many contiguous runs a sorted position list falls into. */
function runsIn(positions: number[]): number {
  let runs = 1;
  for (let index = 1; index < positions.length; index += 1) {
    if (positions[index]! !== positions[index - 1]! + 1) runs += 1;
  }
  return runs;
}

describe("the positions the picture is drawn from", () => {
  it("gives one attention weight per kept position, aligned to it", () => {
    /*
     * The bug this caught. The strip colours `kept[i]` with `attention[i]`,
     * but the vector handed to the policy covers the positions held *before*
     * the step, and eviction then changes that list — so the two were
     * misaligned by however many positions had just been dropped, and every
     * column was painted with a neighbour's weight. The view now looks each
     * weight up by position.
     */
    for (const policy of [H2O, STREAMING_LLM, SLIDING_WINDOW, TOVA]) {
      const { view } = run(policy, 2_000);
      expect(view.attention.length).toBe(view.kept.length);

      // Exactly one position reads zero: the newest, which is the query and
      // pays no attention to itself.
      expect(view.attention.filter((weight) => weight === 0)).toHaveLength(1);
      expect(view.attention.at(-1)).toBe(0);
    }
  });

  it("keeps positions in ascending order", () => {
    const { view } = run(H2O, 2_000);
    for (let index = 1; index < view.kept.length; index += 1) {
      expect(view.kept[index]!).toBeGreaterThan(view.kept[index - 1]!);
    }
  });

  it("holds a contiguous recent block for the sliding window", () => {
    // The shape *is* the policy: a sliding window keeps the last N positions
    // and nothing else, so the strip should show one solid run.
    const { view } = run(SLIDING_WINDOW, 2_000);
    const first = view.kept[0]!;
    view.kept.forEach((position, index) => {
      expect(position).toBe(first + index);
    });
  });

  it("holds both ends and a gap between, for StreamingLLM", () => {
    // The other characteristic shape: a few attention sinks at the very start,
    // a recent window at the end, and nothing in the middle.
    const { view } = run(STREAMING_LLM, 2_000);
    expect(view.kept[0]).toBe(0);

    let biggestGap = 0;
    for (let index = 1; index < view.kept.length; index += 1) {
      biggestGap = Math.max(biggestGap, view.kept[index]! - view.kept[index - 1]!);
    }
    expect(biggestGap).toBeGreaterThan(1);
  });

  it("scatters its kept positions when the policy chooses by attention", () => {
    // TOVA keeps whatever proved heavy wherever it fell, so its holdings are
    // neither one run nor two — a test that would fail if it silently
    // degenerated into a sliding window.
    expect(runsIn(run(TOVA, 2_000).view.kept)).toBeGreaterThan(2);
    expect(runsIn(run(SNAPKV, 2_000).view.kept)).toBeGreaterThan(2);
  });

  /**
   * A result worth pinning, because it is not what the name suggests.
   *
   * H2O selects by accumulated attention and could in principle hold anything.
   * On this trace it holds **two runs** — the same shape as StreamingLLM, which
   * hard-codes "a few sinks at the start plus a recent window". H2O is not
   * imitating it; it is discovering the same answer, because on a decode trace
   * the heaviest hitters really are the early sink positions. The policies
   * still differ in what they retain (0.715 against 0.786), so this is a
   * similarity of shape rather than an equivalence like GCRA and the token
   * bucket.
   */
  it("has H2O arriving at StreamingLLM's shape without being told to", () => {
    expect(runsIn(run(H2O, 2_000).view.kept)).toBe(2);
    expect(runsIn(run(STREAMING_LLM, 2_000).view.kept)).toBe(2);
    expect(run(H2O).metrics["retainedAttentionMass"]).not.toBe(
      run(STREAMING_LLM).metrics["retainedAttentionMass"],
    );
  });

  it("reports the sequence length the strip is scaled against", () => {
    const { view } = run(H2O, 2_000);
    expect(view.sequenceLength).toBe(KV_CACHE_TRACES[TRACE]!.sequenceLength);
    expect(view.position).toBe(2_000);
  });
});
