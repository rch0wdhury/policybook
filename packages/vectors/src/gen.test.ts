import { Rng } from "@policybook/core";
import { describe, expect, it } from "vitest";
import { formatConflicts, generateVectors } from "./gen";
import type { ScenarioFile } from "./gen";
import type { PolicyFactory } from "./run";

/** A tiny FIFO, standing in for a real policy. */
class FakeQueue {
  private items: string[] = [];
  private readonly rng: Rng;

  constructor(_params: Record<string, unknown>, rng: Rng) {
    this.rng = rng;
  }

  push(key: string): void {
    this.items.push(key);
  }

  pop(): string {
    return this.items.shift() ?? "";
  }

  size(): number {
    return this.items.length;
  }

  scores(): Float32Array {
    return new Float32Array([1, 2]);
  }

  infinite(): number {
    return -Infinity;
  }

  draw(): number {
    return this.rng.nextU32();
  }
}

const factory: PolicyFactory = (params, rng) => new FakeQueue(params, rng);

describe("generateVectors", () => {
  it("captures the reference implementation's results", () => {
    const scenarios: ScenarioFile = {
      policy: "fake/queue",
      cases: [
        {
          name: "smoke",
          params: { capacity: 2 },
          seed: 3,
          steps: [
            { call: "push", args: ["a"] },
            { call: "push", args: ["b"] },
            { call: "size", capture: true },
            { call: "pop", capture: true },
          ],
        },
      ],
    };

    const result = generateVectors(factory, scenarios);

    expect(result.conflicts).toEqual([]);
    expect(result.captured).toBe(2);
    expect(result.file.policy).toBe("fake/queue");
    expect(result.file.version).toBe(1);

    const steps = result.file.cases[0]?.steps ?? [];
    expect(steps[2]).toEqual({ call: "size", expect: 2 });
    expect(steps[3]).toEqual({ call: "pop", expect: "a" });
    // A step that neither captures nor asserts stays an unchecked driver step.
    expect(steps[0]).toEqual({ call: "push", args: ["a"] });
    expect(result.file.cases[0]?.seed).toBe(3);
  });

  it("keeps a hand-authored expectation and verifies it", () => {
    const scenarios: ScenarioFile = {
      policy: "fake/queue",
      cases: [
        {
          name: "distinguishing",
          steps: [
            { call: "push", args: ["a"] },
            { call: "pop", expect: "a" },
          ],
        },
      ],
    };

    const result = generateVectors(factory, scenarios);
    expect(result.conflicts).toEqual([]);
    expect(result.verified).toBe(1);
    expect(result.captured).toBe(0);
    expect(result.file.cases[0]?.steps[1]).toEqual({ call: "pop", expect: "a" });
  });

  it("refuses to overwrite a hand-authored expectation the implementation disagrees with", () => {
    // This is the whole point: if the implementation were simply trusted, a bug
    // would be captured as the expectation and the vector would prove nothing.
    const scenarios: ScenarioFile = {
      policy: "fake/queue",
      cases: [
        {
          name: "reasoned from the paper",
          steps: [
            { call: "push", args: ["a"] },
            { call: "push", args: ["b"] },
            { call: "pop", expect: "b" },
          ],
        },
      ],
    };

    const result = generateVectors(factory, scenarios);

    expect(result.conflicts).toHaveLength(1);
    expect(result.conflicts[0]?.call).toBe("pop");
    expect(result.conflicts[0]?.message).toContain('expected "b"');
    // The reviewed value survives in the output; the caller refuses to write it.
    expect(result.file.cases[0]?.steps[2]).toEqual({ call: "pop", expect: "b" });

    const report = formatConflicts("fake/queue", result.conflicts);
    expect(report).toContain("Nothing was written");
    expect(report).toContain("reasoned from the paper");
  });

  it("captures typed arrays as plain JSON arrays", () => {
    const result = generateVectors(factory, {
      policy: "fake/queue",
      cases: [{ name: "typed", steps: [{ call: "scores", capture: true }] }],
    });
    expect(result.file.cases[0]?.steps[0]?.expect).toEqual([1, 2]);
  });

  it("explains why a non-finite result cannot be captured", () => {
    expect(() =>
      generateVectors(factory, {
        policy: "fake/queue",
        cases: [{ name: "inf", steps: [{ call: "infinite", capture: true }] }],
      }),
    ).toThrow(/cannot capture -Infinity/);
  });

  it("explains why a void method cannot be captured", () => {
    expect(() =>
      generateVectors(factory, {
        policy: "fake/queue",
        cases: [{ name: "void", steps: [{ call: "push", args: ["a"], capture: true }] }],
      }),
    ).toThrow(/returned undefined/);
  });

  it("names a method that does not exist", () => {
    expect(() =>
      generateVectors(factory, {
        policy: "fake/queue",
        cases: [{ name: "typo", steps: [{ call: "poop", capture: true }] }],
      }),
    ).toThrow(/no method "poop"/);
  });

  it("seeds each case's Rng so captures are reproducible", () => {
    const scenarios: ScenarioFile = {
      policy: "fake/queue",
      cases: [{ name: "seeded", seed: 42, steps: [{ call: "draw", capture: true }] }],
    };
    const first = generateVectors(factory, scenarios);
    const second = generateVectors(factory, scenarios);
    expect(first.file.cases[0]?.steps[0]?.expect).toBe(new Rng(42).nextU32());
    expect(second.file).toEqual(first.file);
  });
});
