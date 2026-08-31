/**
 * Validates every policy in the registry.
 *
 * The catalog is only as good as its weakest entry, so the rules from
 * and §17 are enforced mechanically rather than left to review:
 * metadata is complete and matches the directory, declared ports have files
 * behind them, the README actually answers "when not to use it", and the
 * vectors include the cases that make them worth having.
 *
 * Shared by `pnpm check` and `policybook check`, so the rules cannot drift
 * between the two.
 */

import { existsSync, readFileSync } from "node:fs";
import { join, relative } from "node:path";
import { discoverPolicies, findRepoRoot } from "./discover";
import type {
  DiscoveredPolicy,
  PolicyMeta,
  VectorsFile,
} from "./types";

/** One rule violation, tied to the file a reader should open. */
export interface Problem {
  file: string;
  message: string;
}

/** Something worth saying that is not yet a failure. */
export interface Warning {
  file: string;
  message: string;
}

const VALID_STATUSES = ["stable", "experimental", "offline-bound"];
const VALID_PORTS = ["ts", "python", "c", "go"];

/** Sections every policy README must carry. */
const REQUIRED_SECTIONS = [
  "When to use it",
  "When not to use it",
  "How it works",
  "Parameters",
  "Complexity",
  "Benchmark",
  "Source",
];

/**
 * Vector cases every policy must have.
 *
 * Matched as a substring of the case name, so a descriptive name like
 * "distinguishing: SIEVE keeps the visited bit where CLOCK does not" counts.
 */
const REQUIRED_CASES = ["smoke", "boundary", "distinguishing", "tiebreak"];

const MAX_SUMMARY_LENGTH = 200;

/** Returns the body of a `## Heading` section, up to the next heading. */
function sectionBody(markdown: string, heading: string): string | null {
  const lines = markdown.split("\n");
  const start = lines.findIndex(
    (line) => line.trim().toLowerCase() === `## ${heading}`.toLowerCase(),
  );
  if (start === -1) return null;

  const body: string[] = [];
  for (let index = start + 1; index < lines.length; index += 1) {
    const line = lines[index] ?? "";
    if (line.startsWith("#")) break;
    body.push(line);
  }
  return body.join("\n").trim();
}

function isPlainObject(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function checkMeta(policy: DiscoveredPolicy, metaFile: string, problems: Problem[]): void {
  const meta = policy.meta as Partial<PolicyMeta>;
  const fail = (message: string): void => {
    problems.push({ file: metaFile, message });
  };

  if (meta.id !== policy.id) {
    fail(`id is "${String(meta.id)}" but the directory says "${policy.id}"`);
  }
  if (meta.domain !== policy.domain) {
    fail(`domain is "${String(meta.domain)}" but the directory says "${policy.domain}"`);
  }
  if (typeof meta.name !== "string" || meta.name.trim() === "") {
    fail("name must be a non-empty string (it is the display name on the site)");
  }

  if (typeof meta.summary !== "string" || meta.summary.trim() === "") {
    fail("summary must be a non-empty sentence — it is the card text and the CLI row");
  } else if (meta.summary.length > MAX_SUMMARY_LENGTH) {
    fail(`summary is ${meta.summary.length} characters; keep it under ${MAX_SUMMARY_LENGTH}`);
  }

  // Source: either a citation, or explicitly folklore.
  const source: unknown = meta.source;
  if (!isPlainObject(source)) {
    fail('source must be a citation object or {"type": "folklore"}');
  } else if (source["type"] === "folklore") {
    // Nothing more to check.
  } else {
    if (typeof source["title"] !== "string" || source["title"] === "") {
      fail("source.title is required for a cited policy");
    }
    if (!Array.isArray(source["authors"]) || source["authors"].length === 0) {
      fail("source.authors must list at least one author");
    }
    for (const field of ["venue", "year", "url"]) {
      if (!(field in source)) fail(`source.${field} is required (use null if unknown)`);
    }
  }

  if (!isPlainObject(meta.complexity)) {
    fail("complexity must be an object with time and space");
  } else {
    for (const field of ["time", "space"]) {
      const value = (meta.complexity as Record<string, unknown>)[field];
      if (typeof value !== "string" || value.trim() === "") {
        fail(`complexity.${field} must be a non-empty string, e.g. "O(1) amortised"`);
      }
    }
  }

  if (!Array.isArray(meta.params)) {
    fail("params must be an array (use [] if the policy takes none)");
  } else {
    meta.params.forEach((param, index) => {
      if (!isPlainObject(param)) {
        fail(`params[${index}] must be an object`);
        return;
      }
      if (typeof param["name"] !== "string" || param["name"] === "") {
        fail(`params[${index}].name is required`);
      }
      if (typeof param["type"] !== "string" || param["type"] === "") {
        fail(`params[${index}].type is required`);
      }
      // Every param has a documented default.
      if (!("default" in param)) {
        fail(`params[${index}] ("${String(param["name"])}") has no default`);
      }
      if (typeof param["description"] !== "string" || param["description"].trim() === "") {
        fail(`params[${index}] ("${String(param["name"])}") needs a description`);
      }
    });
  }

  if (!Array.isArray(meta.tags)) fail("tags must be an array");
  if (typeof meta.recommended !== "boolean") fail("recommended must be true or false");
  if (!(meta.notes === null || typeof meta.notes === "string")) {
    fail("notes must be a string or null");
  }

  if (typeof meta.status !== "string" || !VALID_STATUSES.includes(meta.status)) {
    fail(`status must be one of ${VALID_STATUSES.join(", ")}`);
  }

  // Ports must have files behind them.
  if (!Array.isArray(meta.ports)) {
    fail("ports must be an array");
    return;
  }

  for (const port of meta.ports) {
    if (typeof port !== "string" || !VALID_PORTS.includes(port)) {
      fail(`unknown port "${String(port)}" (expected ${VALID_PORTS.join(", ")})`);
      continue;
    }
    const expected: Record<string, string[]> = {
      ts: ["index.ts"],
      python: ["policy.py"],
      c: ["policy.c", "policy.h"],
      go: [],
    };
    for (const file of expected[port] ?? []) {
      if (!existsSync(join(policy.dir, file))) {
        fail(`ports lists "${port}" but ${file} does not exist`);
      }
    }
  }

  if (meta.status === "stable") {
    for (const required of ["ts", "python", "c"]) {
      if (!meta.ports.includes(required)) {
        fail(`status is "stable", which requires a ${required} port`);
      }
    }
  }
}

function checkReadme(policy: DiscoveredPolicy, problems: Problem[]): void {
  const path = join(policy.dir, "README.md");
  const file = relative(process.cwd(), path);

  if (!existsSync(path)) {
    problems.push({ file, message: "every policy needs a README (template in)" });
    return;
  }

  const markdown = readFileSync(path, "utf8");

  for (const heading of REQUIRED_SECTIONS) {
    const body = sectionBody(markdown, heading);
    if (body === null) {
      problems.push({ file, message: `missing the "## ${heading}" section` });
      continue;
    }

    // The two sections that carry the judgement have to actually say something.
    if (heading === "When to use it" || heading === "When not to use it") {
      const content = body.replace(/\s+/g, " ").trim();
      if (content.length < 40) {
        problems.push({
          file,
          message:
            `"## ${heading}" is empty or near-empty. This is the section readers come for ` +
            "— name the workload shapes, not generalities",
        });
      }
    }
  }

  if (!markdown.includes("<!-- bench:start -->") || !markdown.includes("<!-- bench:end -->")) {
    problems.push({
      file,
      message: "the Benchmark section needs <!-- bench:start --> and <!-- bench:end --> markers",
    });
  }
}

function checkVectors(policy: DiscoveredPolicy, problems: Problem[]): void {
  if (!policy.meta.ports?.includes("ts")) return;

  const file = relative(process.cwd(), policy.vectorsPath);
  if (!existsSync(policy.vectorsPath)) {
    problems.push({ file, message: "a policy with a ts port needs vectors.json" });
    return;
  }

  let vectors: VectorsFile;
  try {
    vectors = JSON.parse(readFileSync(policy.vectorsPath, "utf8")) as VectorsFile;
  } catch (error) {
    problems.push({
      file,
      message: `not valid JSON: ${error instanceof Error ? error.message : String(error)}`,
    });
    return;
  }

  if (vectors.policy !== policy.id) {
    problems.push({ file, message: `declares policy "${vectors.policy}", expected "${policy.id}"` });
  }
  if (typeof vectors.version !== "number") {
    problems.push({ file, message: "version must be a number" });
  }
  if (!Array.isArray(vectors.cases) || vectors.cases.length === 0) {
    problems.push({ file, message: "cases must be a non-empty array" });
    return;
  }

  const names = vectors.cases.map((testCase) => (testCase.name ?? "").toLowerCase());
  for (const required of REQUIRED_CASES) {
    if (!names.some((name) => name.includes(required))) {
      problems.push({
        file,
        message:
          `no case whose name contains "${required}". Every policy needs a smoke case, a ` +
          "boundary case, a case distinguishing it from its nearest neighbour, and a " +
          "tie-break case",
      });
    }
  }

  let assertions = 0;
  vectors.cases.forEach((testCase, index) => {
    if (!Array.isArray(testCase.steps) || testCase.steps.length === 0) {
      problems.push({ file, message: `case ${index} ("${testCase.name}") has no steps` });
      return;
    }
    for (const step of testCase.steps) {
      if (Object.prototype.hasOwnProperty.call(step, "expect")) assertions += 1;
    }
  });

  if (assertions === 0) {
    problems.push({ file, message: "no case asserts anything — every vector file needs expectations" });
  }
}

function checkBench(policy: DiscoveredPolicy, problems: Problem[]): void {
  // "No policy ships as stable without a benchmark row". This
  // was a warning until the bench pipeline existed; now that it does, a stable
  // policy with no measurements is a real gap in the catalog.
  if (policy.meta.status !== "stable") return;
  if (existsSync(join(policy.dir, "bench.json"))) return;
  problems.push({
    file: relative(process.cwd(), join(policy.dir, "bench.json")),
    message:
      'status is "stable" but there is no bench.json. Run `pnpm bench <domain>` and ' +
      "`pnpm render`, or mark the policy experimental until it can be measured.",
  });
}

/** Everything a caller needs to report on the catalog's state. */
export interface CatalogReport {
  policies: DiscoveredPolicy[];
  domains: string[];
  problems: Problem[];
  warnings: Warning[];
}

/**
 * Validate every policy in the registry.
 *
 * Returns findings rather than printing them, so the CLI and the script can
 * present them differently while enforcing exactly the same rules.
 */
export function checkCatalog(repoRoot: string = findRepoRoot()): CatalogReport {
  const policies = discoverPolicies(repoRoot);
  const problems: Problem[] = [];
  const warnings: Warning[] = [];

  for (const policy of policies) {
    const metaFile = relative(process.cwd(), join(policy.dir, "policy.json"));
    checkMeta(policy, metaFile, problems);
    checkReadme(policy, problems);
    checkVectors(policy, problems);
    checkBench(policy, problems);
  }

  // A domain is expected to have an overview README with its decision table.
  // Warned rather than failed: a domain README is written once the domain has
  // policies to tabulate.
  const domains = [...new Set(policies.map((policy) => policy.domain))];
  for (const domain of domains) {
    const path = join(repoRoot, "policies", domain, "README.md");
    if (!existsSync(path)) {
      warnings.push({
        file: relative(process.cwd(), path),
        message: "domain has no README with its decision table",
      });
      continue;
    }

  }

  return { policies, domains, problems, warnings };
}

/** Print a report the way both entry points do, and return an exit code. */
export function reportCatalog(report: CatalogReport): number {
  for (const warning of report.warnings) {
    console.warn(`warn  ${warning.file}: ${warning.message}`);
  }

  if (report.problems.length > 0) {
    console.error(`\ncheck-catalog: ${report.problems.length} problem(s)\n`);
    for (const problem of report.problems) {
      console.error(`  ${problem.file}\n      ${problem.message}`);
    }
    console.error("");
    return 1;
  }

  const suffix = report.warnings.length > 0 ? `, ${report.warnings.length} warning(s)` : "";
  const count = report.policies.length;
  console.log(
    `check-catalog: ${count} polic${count === 1 ? "y" : "ies"} across ` +
      `${report.domains.length} domain(s) — all valid${suffix}.`,
  );
  return 0;
}
