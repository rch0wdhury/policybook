/**
 * The TypeScript vector runner.
 *
 * Every language has one generic runner that reflects on method names, so
 * adding an implementation means adding one file and a registry entry, not a
 * new test (concept.md §12.3). This is that runner for TypeScript; the Python
 * one lives in `packages/python/policybook/_vectors.py` and the C one is
 * generated ahead of time, since C cannot reflect.
 */

import { Rng } from "@policybook/core";
import { compareValues, DEFAULT_TOLERANCE, describeValue } from "./compare";
import type { JsonValue, VectorCase, VectorsFile } from "./types";

/**
 * Builds the policy under test.
 *
 * The registry's convention is that a policy's `index.ts` default-exports a
 * class constructed as `new Policy(params, rng)`: a single params object with a
 * documented default for every field (concept.md §5), and an `Rng` for the
 * policies that need randomness (concept.md §9).
 */
export type PolicyFactory = (params: Record<string, JsonValue>, rng: Rng) => object;

/** One thing that went wrong, located precisely enough to fix. */
export interface VectorFailure {
  caseName: string;
  /** Index into the case's steps, or -1 for a failure during construction. */
  stepIndex: number;
  call: string;
  message: string;
}

/** What a run produced: counts for reporting, failures for asserting. */
export interface VectorRunResult {
  policy: string;
  casesRun: number;
  stepsRun: number;
  /** Steps that carried an `expect` and were therefore checked. */
  assertionsRun: number;
  failures: VectorFailure[];
}

/** Collects callable method names from an object and its prototype chain. */
function methodNames(target: object): string[] {
  const names = new Set<string>();
  let current: object | null = target;
  while (current !== null && current !== Object.prototype) {
    for (const key of Object.getOwnPropertyNames(current)) {
      if (key === "constructor") continue;
      // Read the descriptor rather than the property: a getter would run.
      const descriptor = Object.getOwnPropertyDescriptor(current, key);
      if (descriptor !== undefined && typeof descriptor.value === "function") {
        names.add(key);
      }
    }
    current = Object.getPrototypeOf(current) as object | null;
  }
  return [...names].sort();
}

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

function runCase(
  factory: PolicyFactory,
  testCase: VectorCase,
  failures: VectorFailure[],
): { steps: number; assertions: number } {
  const tolerance = testCase.tolerance ?? DEFAULT_TOLERANCE;
  // Deterministic policies still get a seeded Rng, for uniformity across cases.
  const rng = new Rng(testCase.seed ?? 0);

  let policy: object;
  try {
    policy = factory(testCase.params ?? {}, rng);
  } catch (error) {
    failures.push({
      caseName: testCase.name,
      stepIndex: -1,
      call: "constructor",
      message: `constructing the policy threw: ${errorMessage(error)}`,
    });
    return { steps: 0, assertions: 0 };
  }

  let steps = 0;
  let assertions = 0;

  for (let index = 0; index < testCase.steps.length; index += 1) {
    const step = testCase.steps[index];
    if (step === undefined) continue;

    const method = (policy as Record<string, unknown>)[step.call];
    if (typeof method !== "function") {
      failures.push({
        caseName: testCase.name,
        stepIndex: index,
        call: step.call,
        message:
          `the policy has no method "${step.call}". ` +
          `Implement it, or fix the vector. Available: ${methodNames(policy).join(", ") || "none"}`,
      });
      // The rest of the case depends on state this step would have set.
      break;
    }

    const args = step.args ?? [];
    let actual: unknown;
    try {
      actual = (method as (...callArgs: JsonValue[]) => unknown).apply(policy, args);
    } catch (error) {
      failures.push({
        caseName: testCase.name,
        stepIndex: index,
        call: step.call,
        message: `${step.call}(${args.map(describeValue).join(", ")}) threw: ${errorMessage(error)}`,
      });
      break;
    }
    steps += 1;

    // `expect: null` is a real assertion, so test for the key, not the value.
    if (!Object.prototype.hasOwnProperty.call(step, "expect")) continue;
    assertions += 1;

    const message = compareValues(actual, step.expect, tolerance);
    if (message !== null) {
      failures.push({
        caseName: testCase.name,
        stepIndex: index,
        call: step.call,
        message: `${step.call}(${args.map(describeValue).join(", ")}) — ${message}`,
      });
    }
  }

  return { steps, assertions };
}

/**
 * Runs every case in `file` against the policy built by `factory`.
 *
 * Never throws for a failed expectation: failures are collected so one run
 * reports everything that is wrong, not just the first thing.
 */
export function runVectors(factory: PolicyFactory, file: VectorsFile): VectorRunResult {
  const failures: VectorFailure[] = [];
  let stepsRun = 0;
  let assertionsRun = 0;

  for (const testCase of file.cases) {
    const counts = runCase(factory, testCase, failures);
    stepsRun += counts.steps;
    assertionsRun += counts.assertions;
  }

  return {
    policy: file.policy,
    casesRun: file.cases.length,
    stepsRun,
    assertionsRun,
    failures,
  };
}

/** Formats failures as a block of text suitable for a test report or the CLI. */
export function formatFailures(result: VectorRunResult): string {
  if (result.failures.length === 0) return "";
  const lines = [
    `${result.policy}: ${result.failures.length} of ${result.assertionsRun} assertion(s) failed`,
  ];
  for (const failure of result.failures) {
    const where = failure.stepIndex === -1 ? "construction" : `step ${failure.stepIndex}`;
    lines.push(`  [${failure.caseName}] ${where}: ${failure.message}`);
  }
  return lines.join("\n");
}

/** Runs the vectors and throws a formatted error if anything failed. */
export function assertVectors(factory: PolicyFactory, file: VectorsFile): VectorRunResult {
  const result = runVectors(factory, file);
  if (result.failures.length > 0) throw new Error(formatFailures(result));
  return result;
}
