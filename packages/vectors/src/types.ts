/**
 * The shape of a `vectors.json` file and of the `policy.json` metadata beside it.
 *
 * Test vectors are the contract between languages: a list of method calls and
 * expected results, language-neutral by construction. A port
 * is conformant when it reproduces them exactly. Nothing in this file is
 * TypeScript-specific — Python and C read the same JSON.
 */

/** Any value expressible in JSON. */
export type JsonValue =
  | string
  | number
  | boolean
  | null
  | JsonValue[]
  | { [key: string]: JsonValue };

/**
 * One method call in a case.
 *
 * `expect` is compared against the return value. A step without `expect` is
 * still executed — most steps exist to drive the policy into a state, and only
 * some of them assert. Note that `expect: null` is a real assertion (several
 * interfaces return `null` meaningfully), so presence of the key is what
 * matters, not whether the value is nullish.
 */
export interface VectorStep {
  /** Method name on the policy, in the reference (camelCase) spelling. */
  call: string;
  /** Positional arguments. Defaults to none. */
  args?: JsonValue[];
  /** Expected return value, compared with deep equality. */
  expect?: JsonValue;
}

/** One scenario: construct the policy, run the steps, check the expectations. */
export interface VectorCase {
  /** Human-readable name; also the label in test output. */
  name: string;
  /** Constructor params. Every param has a default, so this may be partial. */
  params?: Record<string, JsonValue>;
  /** Seed for the case's `Rng`. Deterministic policies still set one. */
  seed?: number;
  /** Absolute tolerance for float comparisons in this case. Default 1e-9. */
  tolerance?: number;
  steps: VectorStep[];
}

/** A policy's complete vector file. */
export interface VectorsFile {
  /** Policy id, e.g. `cache/sieve`. Must match the directory it lives in. */
  policy: string;
  version: number;
  cases: VectorCase[];
}

/** Citation for the paper or folklore a policy comes from. */
export type PolicySource =
  | { type: "folklore" }
  | {
      title: string;
      authors: string[];
      venue: string | null;
      year: number | null;
      url: string | null;
    };

/** One constructor parameter, as documented in `policy.json`. */
export interface PolicyParam {
  name: string;
  type: string;
  default: JsonValue;
  description: string;
}

/** Everything `policy.json` declares about a policy. */
export interface PolicyMeta {
  id: string;
  name: string;
  domain: string;
  summary: string;
  source: PolicySource;
  complexity: { time: string; space: string };
  params: PolicyParam[];
  tags: string[];
  recommended: boolean;
  ports: string[];
  notes: string | null;
  status: "stable" | "experimental" | "offline-bound";
}

/** A policy found on disk by {@link discoverPolicies}. */
export interface DiscoveredPolicy {
  /** `<domain>/<policy>`, derived from the directory path. */
  id: string;
  domain: string;
  name: string;
  /** Absolute path to the policy directory. */
  dir: string;
  meta: PolicyMeta;
  /** Absolute path to `vectors.json`; may not exist yet for a new policy. */
  vectorsPath: string;
  /** Absolute path to the TypeScript entry point, `index.ts`. */
  entry: string;
}
