/**
 * @policybook/vectors — the TypeScript vector runner.
 *
 * A policy's `vectors.json` is a list of method calls and expected results
 *. This package replays one against a TypeScript
 * implementation, and knows how to find every policy in the registry.
 */

export { checkCatalog, reportCatalog } from "./catalog";
export type { CatalogReport, Problem, Warning } from "./catalog";
export { compareValues, describeValue, DEFAULT_TOLERANCE } from "./compare";
export { discoverPolicies, findRepoRoot, loadFactory, loadVectors } from "./discover";
export { assertVectors, formatFailures, runVectors } from "./run";
export type { PolicyFactory, VectorFailure, VectorRunResult } from "./run";
export type {
  DiscoveredPolicy,
  JsonValue,
  PolicyMeta,
  PolicyParam,
  PolicySource,
  VectorCase,
  VectorStep,
  VectorsFile,
} from "./types";
