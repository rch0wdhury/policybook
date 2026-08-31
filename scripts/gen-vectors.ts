/**
 * Regenerates `vectors.json` for one policy, a domain, or the whole registry.
 *
 * Each policy keeps a committed `vectors.gen.ts` scenario script beside it.
 * This runs those scenarios against the reference TypeScript implementation,
 * captures the results, and writes the vectors file. Hand-authored
 * expectations are verified, never overwritten — if the implementation
 * disagrees with one, nothing is written and the disagreement is reported
 *.
 *
 * Usage:
 *   pnpm gen:vectors cache/sieve     # one policy
 *   pnpm gen:vectors cache           # every policy in a domain
 *   pnpm gen:vectors --all           # everything
 */

import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { pathToFileURL } from "node:url";
import { stableJson } from "../packages/core/src/json";
import { discoverPolicies, findRepoRoot, loadFactory } from "../packages/vectors/src/discover";
import { formatConflicts, generateVectors } from "../packages/vectors/src/gen";
import type { ScenarioFile } from "../packages/vectors/src/gen";
import type { DiscoveredPolicy } from "../packages/vectors/src/types";

function selectPolicies(all: DiscoveredPolicy[], targets: string[]): DiscoveredPolicy[] {
  if (targets.includes("--all")) return all;
  if (targets.length === 0) {
    throw new Error(
      "name a policy (cache/sieve), a domain (cache), or --all.\n" +
        "Usage: pnpm gen:vectors <id|domain|--all>",
    );
  }

  const selected: DiscoveredPolicy[] = [];
  for (const target of targets) {
    const matches = all.filter(
      (policy) => policy.id === target || policy.domain === target,
    );
    if (matches.length === 0) {
      const known = all.map((policy) => policy.id).join(", ") || "the catalog is empty";
      throw new Error(`no policy or domain called "${target}". Known: ${known}`);
    }
    for (const match of matches) {
      if (!selected.includes(match)) selected.push(match);
    }
  }
  return selected;
}

async function main(): Promise<void> {
  const targets = process.argv.slice(2);
  const repoRoot = findRepoRoot();
  const policies = selectPolicies(discoverPolicies(repoRoot), targets);

  let written = 0;
  let unchanged = 0;
  const failed: string[] = [];

  for (const policy of policies) {
    const scenarioPath = join(policy.dir, "vectors.gen.ts");
    if (!existsSync(scenarioPath)) {
      failed.push(`${policy.id}: no vectors.gen.ts (every policy needs a scenario script)`);
      continue;
    }

    const module = (await import(pathToFileURL(scenarioPath).href)) as {
      default?: ScenarioFile;
    };
    const scenarios = module.default;
    if (scenarios === undefined) {
      failed.push(`${policy.id}: vectors.gen.ts must default-export its scenarios`);
      continue;
    }
    if (scenarios.policy !== policy.id) {
      failed.push(
        `${policy.id}: vectors.gen.ts declares policy "${scenarios.policy}", ` +
          "which does not match its directory",
      );
      continue;
    }

    const factory = await loadFactory(policy.entry);
    const result = generateVectors(factory, scenarios);

    if (result.conflicts.length > 0) {
      failed.push(formatConflicts(policy.id, result.conflicts));
      continue;
    }

    const serialised = stableJson(result.file);
    const existing = existsSync(policy.vectorsPath)
      ? readFileSync(policy.vectorsPath, "utf8")
      : null;

    if (existing === serialised) {
      unchanged += 1;
      console.log(
        `  ${policy.id}: unchanged (${result.captured} captured, ${result.verified} hand-authored)`,
      );
      continue;
    }

    writeFileSync(policy.vectorsPath, serialised);
    written += 1;
    console.log(
      `  ${policy.id}: written (${result.captured} captured, ${result.verified} hand-authored)`,
    );
  }

  if (failed.length > 0) {
    console.error(`\n${failed.join("\n\n")}`);
    process.exit(1);
  }

  console.log(`\ngen:vectors — ${written} written, ${unchanged} unchanged.`);
}

main().catch((error: unknown) => {
  console.error(error instanceof Error ? error.message : String(error));
  process.exit(1);
});
