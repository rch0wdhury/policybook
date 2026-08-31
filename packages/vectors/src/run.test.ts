import { Rng } from "@policybook/core";
import { describe, expect, it } from "vitest";
import { compareValues } from "./compare";
import { formatFailures, runVectors } from "./run";
import type { PolicyFactory } from "./run";
import type { VectorsFile } from "./types";

/** A stand-in policy with one method of each interesting shape. */
class FakePolicy {
  private readonly capacity: number;
  private readonly rng: Rng;
  private accesses: string[] = [];

  constructor(params: { capacity?: number }, rng: Rng) {
    this.capacity = params.capacity ?? 10;
    this.rng = rng;
  }

  onAccess(key: string): void {
    this.accesses.push(key);
  }

  evict(): string {
    return this.accesses.shift() ?? "";
  }

  size(): number {
    return this.accesses.length;
  }

  configuredCapacity(): number {
    return this.capacity;
  }

  ratio(): number {
    return 1 / 3;
  }

  positions(): number[] {
    return [1, 2, 3];
  }

  weights(): Float32Array {
    return new Float32Array([0.5, 0.25]);
  }

  /** Several interfaces return null meaningfully; this exercises that path. */
  maybe(): number | null {
    return null;
  }

  draw(): number {
    return this.rng.nextU32();
  }

  explode(): never {
    throw new Error("boom");
  }
}

const factory: PolicyFactory = (params, rng) => new FakePolicy(params as { capacity?: number }, rng);

function file(cases: VectorsFile["cases"]): VectorsFile {
  return { policy: "fake/policy", version: 1, cases };
}

describe("runVectors", () => {
  it("runs a passing case and counts steps and assertions", () => {
    const result = runVectors(
      factory,
      file([
        {
          name: "smoke",
          params: { capacity: 3 },
          seed: 1,
          steps: [
            { call: "onAccess", args: ["a"] },
            { call: "onAccess", args: ["b"] },
            { call: "size", expect: 2 },
            { call: "evict", expect: "a" },
            { call: "size", expect: 1 },
          ],
        },
      ]),
    );

    expect(result.failures).toEqual([]);
    expect(result.casesRun).toBe(1);
    expect(result.stepsRun).toBe(5);
    expect(result.assertionsRun).toBe(3);
  });

  it("reports a mismatch with the step, the call and both values", () => {
    const result = runVectors(
      factory,
      file([
        {
          name: "wrong victim",
          steps: [
            { call: "onAccess", args: ["a"] },
            { call: "evict", expect: "z" },
          ],
        },
      ]),
    );

    expect(result.failures).toHaveLength(1);
    const failure = result.failures[0];
    expect(failure?.caseName).toBe("wrong victim");
    expect(failure?.stepIndex).toBe(1);
    expect(failure?.call).toBe("evict");
    expect(failure?.message).toContain('expected "z"');
    expect(failure?.message).toContain('got "a"');
  });

  it("collects failures across cases instead of stopping at the first", () => {
    const result = runVectors(
      factory,
      file([
        { name: "one", steps: [{ call: "size", expect: 99 }] },
        { name: "two", steps: [{ call: "size", expect: 98 }] },
      ]),
    );
    expect(result.failures).toHaveLength(2);
    expect(result.failures.map((entry) => entry.caseName)).toEqual(["one", "two"]);
  });

  it("names the missing method and lists what the policy does have", () => {
    const result = runVectors(
      factory,
      file([{ name: "typo", steps: [{ call: "onAcess", args: ["a"] }] }]),
    );

    expect(result.failures).toHaveLength(1);
    const message = result.failures[0]?.message ?? "";
    expect(message).toContain('no method "onAcess"');
    expect(message).toContain("onAccess");
    expect(message).toContain("evict");
  });

  it("stops a case after a missing method rather than cascading", () => {
    const result = runVectors(
      factory,
      file([
        {
          name: "typo then more",
          steps: [
            { call: "nope" },
            { call: "size", expect: 12345 },
          ],
        },
      ]),
    );
    // Only the missing method is reported; the dependent step is not run.
    expect(result.failures).toHaveLength(1);
  });

  it("reports a method that throws", () => {
    const result = runVectors(
      factory,
      file([{ name: "throws", steps: [{ call: "explode" }] }]),
    );
    expect(result.failures[0]?.message).toContain("threw: boom");
  });

  it("reports a constructor that throws", () => {
    const failing: PolicyFactory = () => {
      throw new Error("bad params");
    };
    const result = runVectors(failing, file([{ name: "ctor", steps: [{ call: "size" }] }]));
    expect(result.failures[0]?.stepIndex).toBe(-1);
    expect(result.failures[0]?.message).toContain("constructing the policy threw: bad params");
  });

  it("treats `expect: null` as a real assertion", () => {
    // The runner keys on presence of the `expect` field, not truthiness — the
    // scheduler and retry interfaces both return null meaningfully.
    const passing = runVectors(
      factory,
      file([{ name: "null ok", steps: [{ call: "maybe", expect: null }] }]),
    );
    expect(passing.failures).toEqual([]);
    expect(passing.assertionsRun).toBe(1);

    const failing = runVectors(
      factory,
      file([{ name: "null bad", steps: [{ call: "size", expect: null }] }]),
    );
    expect(failing.failures).toHaveLength(1);
  });

  it("passes params through to the constructor", () => {
    const result = runVectors(
      factory,
      file([
        {
          name: "params",
          params: { capacity: 7 },
          steps: [{ call: "configuredCapacity", expect: 7 }],
        },
      ]),
    );
    expect(result.failures).toEqual([]);
  });

  it("seeds the policy's Rng from the case", () => {
    const expected = new Rng(42).nextU32();
    const result = runVectors(
      factory,
      file([{ name: "seeded", seed: 42, steps: [{ call: "draw", expect: expected }] }]),
    );
    expect(result.failures).toEqual([]);

    const wrongSeed = runVectors(
      factory,
      file([{ name: "seeded", seed: 1, steps: [{ call: "draw", expect: expected }] }]),
    );
    expect(wrongSeed.failures).toHaveLength(1);
  });

  it("compares arrays and typed arrays against plain JSON arrays", () => {
    const result = runVectors(
      factory,
      file([
        {
          name: "arrays",
          steps: [
            { call: "positions", expect: [1, 2, 3] },
            { call: "weights", expect: [0.5, 0.25] },
          ],
        },
      ]),
    );
    expect(result.failures).toEqual([]);
  });

  it("honours the default float tolerance and a per-case override", () => {
    const oneThird = 0.3333333333;
    // Off by ~3.3e-11, inside the 1e-9 default.
    const withinDefault = runVectors(
      factory,
      file([{ name: "close", steps: [{ call: "ratio", expect: oneThird }] }]),
    );
    expect(withinDefault.failures).toEqual([]);

    // Same value, tolerance tightened past the difference.
    const tightened = runVectors(
      factory,
      file([
        { name: "tight", tolerance: 1e-12, steps: [{ call: "ratio", expect: oneThird }] },
      ]),
    );
    expect(tightened.failures).toHaveLength(1);
    expect(tightened.failures[0]?.message).toContain("tolerance 1e-12");
  });

  it("formats failures into a readable report", () => {
    const result = runVectors(
      factory,
      file([{ name: "bad", steps: [{ call: "size", expect: 42 }] }]),
    );
    const report = formatFailures(result);
    expect(report).toContain("fake/policy");
    expect(report).toContain("[bad]");
    expect(report).toContain("step 0");
    expect(formatFailures({ ...result, failures: [] })).toBe("");
  });
});

describe("compareValues", () => {
  it("accepts equal primitives", () => {
    expect(compareValues(1, 1)).toBeNull();
    expect(compareValues("a", "a")).toBeNull();
    expect(compareValues(true, true)).toBeNull();
    expect(compareValues(null, null)).toBeNull();
  });

  it("handles infinities exactly, which sampling policies rely on", () => {
    expect(compareValues(-Infinity, -Infinity)).toBeNull();
    expect(compareValues(Infinity, Infinity)).toBeNull();
    expect(compareValues(-Infinity, Infinity)).not.toBeNull();
    expect(compareValues([1, -Infinity], [1, -Infinity])).toBeNull();
  });

  it("treats NaN as equal to NaN", () => {
    expect(compareValues(NaN, NaN)).toBeNull();
  });

  it("reports the index of the first differing element", () => {
    const message = compareValues([1, 2, 3], [1, 9, 3]) ?? "";
    expect(message).toContain("result[1]");
  });

  it("reports a length difference", () => {
    const message = compareValues([1, 2], [1, 2, 3]) ?? "";
    expect(message).toContain("expected 3 element(s), got 2");
  });

  it("compares nested objects and names the path", () => {
    expect(compareValues({ a: { b: 1 } }, { a: { b: 1 } })).toBeNull();
    const message = compareValues({ a: { b: 1 } }, { a: { b: 2 } }) ?? "";
    expect(message).toContain("result.a.b");
  });

  it("reports missing and unexpected object keys", () => {
    const message = compareValues({ a: 1 }, { a: 1, b: 2 }) ?? "";
    expect(message).toContain("missing b");
    const extra = compareValues({ a: 1, c: 3 }, { a: 1 }) ?? "";
    expect(extra).toContain("unexpected c");
  });

  it("reports a type mismatch legibly", () => {
    expect(compareValues("2", 2)).toContain("expected the number 2");
    expect(compareValues(undefined, 2)).toContain("got undefined");
    expect(compareValues(5, [5])).toContain("expected an array of 1");
  });
});
