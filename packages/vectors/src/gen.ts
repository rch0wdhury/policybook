/**
 * Turns a policy's scenario script into its committed `vectors.json`.
 *
 * Vectors are generated from the reference TypeScript implementation and then
 * reviewed by hand. That has one obvious hazard: if the
 * implementation is wrong, the captured expectations are wrong in exactly the
 * same way, and the vectors prove nothing. So a scenario step comes in two
 * flavours:
 *
 * - `capture: true` — record whatever the implementation returns. Fine for the
 *   bulk of a trace, where the point is to pin behaviour down, not to argue it.
 * - `expect: <value>` — a hand-authored expectation, reasoned from the paper.
 *   The generator **never overwrites these**; it checks them and refuses to
 *   write the file if the implementation disagrees. This is what makes the
 *   distinguishing case the catalog requires worth having.
 */

import { Rng } from "@policybook/core";
import { compareValues, DEFAULT_TOLERANCE, describeValue } from "./compare";
import type { PolicyFactory } from "./run";
import type { JsonValue, VectorCase, VectorStep, VectorsFile } from "./types";

/** A step in a scenario script. */
export interface ScenarioStep {
  call: string;
  args?: JsonValue[];
  /** Record the reference implementation's return value as the expectation. */
  capture?: boolean;
  /** A hand-authored expectation, checked rather than captured. */
  expect?: JsonValue;
}

export interface ScenarioCase {
  name: string;
  params?: Record<string, JsonValue>;
  seed?: number;
  tolerance?: number;
  steps: ScenarioStep[];
}

/** The default export of a policy's `vectors.gen.ts`. */
export interface ScenarioFile {
  policy: string;
  version?: number;
  cases: ScenarioCase[];
}

/** A hand-authored expectation the implementation disagreed with. */
export interface ScenarioConflict {
  caseName: string;
  stepIndex: number;
  call: string;
  message: string;
}

export interface GenerateResult {
  file: VectorsFile;
  conflicts: ScenarioConflict[];
  captured: number;
  verified: number;
}

/** Converts a return value into something JSON can hold, or explains why it cannot. */
function toJsonValue(value: unknown, context: string): JsonValue {
  if (value === null) return null;
  if (typeof value === "string" || typeof value === "boolean") return value;
  if (typeof value === "number") {
    if (!Number.isFinite(value)) {
      throw new Error(
        `${context}: cannot capture ${describeValue(value)} — JSON has no representation for it. ` +
          "Hand-author this expectation instead.",
      );
    }
    return value;
  }
  if (value === undefined) {
    throw new Error(
      `${context}: the method returned undefined, so there is nothing to capture. ` +
        "Drop `capture: true` from this step — it drives state rather than asserting.",
    );
  }
  if (ArrayBuffer.isView(value) && !(value instanceof DataView)) {
    return Array.from(value as unknown as ArrayLike<number>);
  }
  if (Array.isArray(value)) {
    return value.map((element, index) => toJsonValue(element, `${context}[${index}]`));
  }
  if (typeof value === "object") {
    const result: Record<string, JsonValue> = {};
    for (const [key, nested] of Object.entries(value as Record<string, unknown>)) {
      result[key] = toJsonValue(nested, `${context}.${key}`);
    }
    return result;
  }
  throw new Error(`${context}: cannot capture a value of type ${typeof value}`);
}

/**
 * Runs the scenarios against `factory` and builds the vectors file.
 *
 * Returns conflicts rather than throwing, so the caller can report all of them
 * at once.
 */
export function generateVectors(
  factory: PolicyFactory,
  scenarios: ScenarioFile,
): GenerateResult {
  const conflicts: ScenarioConflict[] = [];
  const cases: VectorCase[] = [];
  let captured = 0;
  let verified = 0;

  for (const scenario of scenarios.cases) {
    const tolerance = scenario.tolerance ?? DEFAULT_TOLERANCE;
    const policy = factory(scenario.params ?? {}, new Rng(scenario.seed ?? 0));
    const steps: VectorStep[] = [];

    for (let index = 0; index < scenario.steps.length; index += 1) {
      const step = scenario.steps[index];
      if (step === undefined) continue;

      const method = (policy as Record<string, unknown>)[step.call];
      if (typeof method !== "function") {
        throw new Error(
          `${scenarios.policy} case "${scenario.name}" step ${index}: ` +
            `the policy has no method "${step.call}".`,
        );
      }

      const args = step.args ?? [];
      const actual = (method as (...callArgs: JsonValue[]) => unknown).apply(policy, args);

      const emitted: VectorStep = { call: step.call };
      if (step.args !== undefined) emitted.args = step.args;

      const hasHandAuthored = Object.prototype.hasOwnProperty.call(step, "expect");
      if (hasHandAuthored) {
        // Keep the reviewed value; the implementation has to match it.
        emitted.expect = step.expect as JsonValue;
        verified += 1;
        const message = compareValues(actual, step.expect, tolerance);
        if (message !== null) {
          conflicts.push({
            caseName: scenario.name,
            stepIndex: index,
            call: step.call,
            message,
          });
        }
      } else if (step.capture === true) {
        emitted.expect = toJsonValue(
          actual,
          `${scenarios.policy} case "${scenario.name}" step ${index} (${step.call})`,
        );
        captured += 1;
      }

      steps.push(emitted);
    }

    // Key order here is the key order in the committed file, so it is fixed
    // deliberately rather than left to however the object was assembled.
    const emittedCase: VectorCase = {
      name: scenario.name,
      ...(scenario.params !== undefined ? { params: scenario.params } : {}),
      seed: scenario.seed ?? 0,
      ...(scenario.tolerance !== undefined ? { tolerance: scenario.tolerance } : {}),
      steps,
    };
    cases.push(emittedCase);
  }

  return {
    file: { policy: scenarios.policy, version: scenarios.version ?? 1, cases },
    conflicts,
    captured,
    verified,
  };
}

/** Formats conflicts for the terminal. */
export function formatConflicts(policy: string, conflicts: ScenarioConflict[]): string {
  const lines = [
    `${policy}: ${conflicts.length} hand-authored expectation(s) the implementation disagrees with.`,
    "Nothing was written. Either the implementation is wrong, or the expectation is —",
    "work out which before regenerating.",
    "",
  ];
  for (const conflict of conflicts) {
    lines.push(`  [${conflict.caseName}] step ${conflict.stepIndex} (${conflict.call}): ${conflict.message}`);
  }
  return lines.join("\n");
}
