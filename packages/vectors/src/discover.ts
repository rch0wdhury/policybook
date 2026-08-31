/**
 * Finds the policies in the registry.
 *
 * The catalog is the directory tree: `policies/<domain>/<policy>/`. Nothing
 * maintains a separate index that could drift, so adding a policy means adding
 * a directory. This module is the single reader of that
 * layout — the vector suite, the catalog validator and the CLI all go through
 * it.
 */

import { existsSync, readFileSync, readdirSync, statSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import type { DiscoveredPolicy, PolicyMeta, VectorsFile } from "./types";
import type { PolicyFactory } from "./run";

/**
 * Walks up from `start` to the registry root.
 *
 * Identified by `pnpm-workspace.yaml` *and* a `policies/` directory. The pair
 * matters: keying on the workspace file alone made the CLI, run inside any
 * other pnpm monorepo, "find" that project's root and then report every
 * policy as unknown instead of saying the registry is not here.
 */
export function findRepoRoot(start: string = dirname(fileURLToPath(import.meta.url))): string {
  let current = resolve(start);
  for (;;) {
    if (
      existsSync(join(current, "pnpm-workspace.yaml")) &&
      existsSync(join(current, "policies"))
    ) {
      return current;
    }
    const parent = dirname(current);
    if (parent === current) {
      throw new Error(
        `could not find the repository root above ${start} ` +
          "(looked for pnpm-workspace.yaml next to a policies/ directory in every parent)",
      );
    }
    current = parent;
  }
}

function directoriesIn(path: string): string[] {
  if (!existsSync(path)) return [];
  return readdirSync(path)
    .filter((entry) => !entry.startsWith("."))
    .filter((entry) => statSync(join(path, entry)).isDirectory())
    .sort();
}

/**
 * Every policy in the registry, sorted by id.
 *
 * A directory without a readable `policy.json` is reported as an error rather
 * than skipped: a half-created policy should be loud, not invisible.
 */
export function discoverPolicies(repoRoot: string = findRepoRoot()): DiscoveredPolicy[] {
  const policiesRoot = join(repoRoot, "policies");
  const found: DiscoveredPolicy[] = [];

  for (const domain of directoriesIn(policiesRoot)) {
    for (const name of directoriesIn(join(policiesRoot, domain))) {
      const dir = join(policiesRoot, domain, name);
      const metaPath = join(dir, "policy.json");
      if (!existsSync(metaPath)) {
        throw new Error(
          `${dir} has no policy.json. Every policy directory needs one ` +
            "(run `policybook new` to scaffold a policy correctly).",
        );
      }

      let meta: PolicyMeta;
      try {
        meta = JSON.parse(readFileSync(metaPath, "utf8")) as PolicyMeta;
      } catch (error) {
        throw new Error(
          `${metaPath} is not valid JSON: ${error instanceof Error ? error.message : String(error)}`,
        );
      }

      found.push({
        id: `${domain}/${name}`,
        domain,
        name,
        dir,
        meta,
        vectorsPath: join(dir, "vectors.json"),
        entry: join(dir, "index.ts"),
      });
    }
  }

  return found;
}

/** Reads and parses a policy's `vectors.json`. */
export function loadVectors(path: string): VectorsFile {
  const raw = readFileSync(path, "utf8");
  try {
    return JSON.parse(raw) as VectorsFile;
  } catch (error) {
    throw new Error(
      `${path} is not valid JSON: ${error instanceof Error ? error.message : String(error)}`,
    );
  }
}

/**
 * Imports a policy's entry point and returns a factory for it.
 *
 * The convention: `index.ts` default-exports the policy class, constructed as
 * `new Policy(params, rng)`. A module may instead export a `createPolicy`
 * function with the same signature, which is what a policy needs when it is
 * more naturally written as a closure than a class.
 */
export async function loadFactory(entry: string): Promise<PolicyFactory> {
  const module = (await import(pathToFileURL(entry).href)) as Record<string, unknown>;

  const created = module["createPolicy"];
  if (typeof created === "function") return created as PolicyFactory;

  const exported = module["default"];
  if (typeof exported === "function") {
    const PolicyClass = exported as new (...args: unknown[]) => object;
    return (params, rng) => new PolicyClass(params, rng);
  }

  throw new Error(
    `${entry} must default-export the policy class (constructed as ` +
      "`new Policy(params, rng)`) or export a `createPolicy(params, rng)` function; " +
      `found exports: ${Object.keys(module).join(", ") || "none"}`,
  );
}
