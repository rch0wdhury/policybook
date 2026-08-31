import { describe, expect, it } from "vitest";
import { formatFailures, runVectors } from "../../../packages/vectors/src/run";
import type { VectorsFile } from "../../../packages/vectors/src/types";
import Constant from "./index";
import vectors from "./vectors.json";

const RETRYABLE = { status: 503, retryable: true };

describe("retry/constant", () => {
  it("passes its vectors", () => {
    const result = runVectors((params) => new Constant(params), vectors as VectorsFile);
    if (result.failures.length > 0) throw new Error(formatFailures(result));
    expect(result.assertionsRun).toBeGreaterThan(0);
  });

  it("rejects parameters that are out of range", () => {
    expect(() => new Constant({ baseMs: -1 })).toThrow(RangeError);
    expect(() => new Constant({ baseMs: 1.5 })).toThrow(RangeError);
    expect(() => new Constant({ maxAttempts: 0 })).toThrow(RangeError);
  });

  it("defaults to 100 ms and eight attempts", () => {
    const policy = new Constant();
    for (let attempt = 1; attempt <= 7; attempt += 1) {
      expect(policy.nextDelay(attempt, RETRYABLE)).toBe(100);
    }
    expect(policy.nextDelay(8, RETRYABLE)).toBeNull();
  });

  it("covers only baseMs x (maxAttempts - 1) of outage, which is its real limit", () => {
    // Eight attempts at a hundred milliseconds is seven hundred milliseconds of
    // patience. Most outages outlast that, and the benchmark says so.
    const policy = new Constant();
    let total = 0;
    for (let attempt = 1; ; attempt += 1) {
      const delay = policy.nextDelay(attempt, RETRYABLE);
      if (delay === null) break;
      total += delay;
    }
    expect(total).toBe(700);
  });

  it("answers identically however many times it is asked", () => {
    // No state and no draw: this is what makes every client retry in lockstep.
    const policy = new Constant({ baseMs: 250, maxAttempts: 5 });
    const answers = new Set<number | null>();
    for (let i = 0; i < 20; i += 1) answers.add(policy.nextDelay(2, RETRYABLE));
    expect(answers).toEqual(new Set([250]));
  });

  it("gives up on a permanent failure regardless of the attempt budget", () => {
    const policy = new Constant({ baseMs: 100, maxAttempts: 100 });
    expect(policy.nextDelay(1, { status: 400, retryable: false })).toBeNull();
    expect(policy.nextDelay(1, RETRYABLE)).toBe(100);
  });
});
