/**
 * Turning a registry policy into a file you can drop into your own project.
 *
 * The recommended way to use this registry is to copy a policy in, not to
 * depend on a package. That only works if what lands in your
 * repository is *self-contained*: `policybook add` must never leave an import
 * pointing back at something you do not have.
 *
 * Policies are allowed to share the deterministic Rng and the C data
 * structures, so those shared modules are inlined here. The whole helper
 * module is copied rather than the one function used from it: a tree-shaker
 * that silently dropped a transitively-needed constant would produce a file
 * that compiles and computes the wrong answer, which is far worse than a few
 * extra lines a reader can see and delete.
 */

import { existsSync, readFileSync } from "node:fs";
import { dirname, join, relative, resolve, sep } from "node:path";
import type { DiscoveredPolicy } from "@policybook/vectors";

/** A C identifier fragment: `w-tinylfu` → `w_tinylfu`. */
function identifier(name: string): string {
  return name.replace(/-/g, "_");
}

function banner(policy: DiscoveredPolicy, comment: "//" | "#" | "*"): string[] {
  const lines = [
    `${policy.meta.name} — ${policy.meta.summary}`,
    "",
    `Copied from the Policybook registry: ${policy.id}`,
    `Source, benchmarks and the full explainer:`,
    `  https://github.com/rch0wdhury/policybook/tree/main/policies/${policy.id}`,
    "",
    "This header is generated. Everything below it is yours: edit freely, and",
    "re-run `policybook add` only if you want to start again from the original.",
  ];

  if (comment === "*") {
    return ["/*", ...lines.map((line) => (line === "" ? " *" : ` * ${line}`)), " */", ""];
  }
  return [...lines.map((line) => (line === "" ? comment : `${comment} ${line}`)), ""];
}

// --- TypeScript ---------------------------------------------------------------

export function inlineTypeScript(policy: DiscoveredPolicy, repoRoot: string): string {
  const entry = join(policy.dir, "index.ts");
  const source = readFileSync(entry, "utf8");

  const helpers: string[] = [];
  const body = source
    .split("\n")
    .filter((line) => {
      const match = /^import\s+.*\s+from\s+"([^"]+)"/.exec(line.trim());
      if (match === null) return true;

      const specifier = match[1]!;
      if (!specifier.startsWith(".")) {
        throw new Error(
          `${policy.id}/index.ts imports "${specifier}", which is not a repository path. ` +
            "A policy may only import shared helpers from the registry.",
        );
      }

      const resolved = resolve(dirname(entry), specifier);
      if (!existsSync(resolved)) {
        throw new Error(`${policy.id}/index.ts imports "${specifier}", which does not exist`);
      }

      // A policy may share the core's helpers, never another policy. Inlining a
      // sibling would splice in its `export default class`, and stripping the
      // `export` from that leaves `default class` — a syntax error in a file
      // that this command has just called self-contained. Restate the helper in
      // both policies and pin their agreement with a test instead.
      const fromPolicies = resolve(repoRoot, "policies");
      if (resolved.startsWith(fromPolicies + sep)) {
        throw new Error(
          `${policy.id}/index.ts imports "${specifier}", which is another policy.\n` +
            "A policy may only import shared helpers from packages/. Copy the helper into\n" +
            "both policies and add a test asserting they agree — a copied file cannot\n" +
            "reach back into the registry it came from.",
        );
      }

      helpers.push(
        [
          "",
          `// --- inlined from ${relative(repoRoot, resolved).replace(/\\/g, "/")} ---`,
          "",
          // `export` would make these part of the caller's public surface.
          readFileSync(resolved, "utf8").replace(/^export /gm, "").trim(),
          "",
          "// --- end of inlined helpers ---",
        ].join("\n"),
      );
      return false;
    })
    .join("\n");

  return [...banner(policy, "//"), ...helpers, body.trim(), ""].join("\n");
}

// --- Python -------------------------------------------------------------------

export function inlinePython(policy: DiscoveredPolicy, repoRoot: string): string {
  const entry = join(policy.dir, "policy.py");
  const source = readFileSync(entry, "utf8");
  const packageRoot = join(repoRoot, "packages", "python");

  const helpers: string[] = [];
  const body = source
    .split("\n")
    .filter((line) => {
      const match = /^from\s+(policybook[.\w]*)\s+import\s+/.exec(line.trim());
      if (match === null) return true;

      const modulePath = join(packageRoot, match[1]!.replace(/\./g, "/") + ".py");
      if (!existsSync(modulePath)) {
        throw new Error(`${policy.id}/policy.py imports ${match[1]!}, which does not exist`);
      }

      // Drop the helper's own docstring and __all__: this file has its own.
      const helper = readFileSync(modulePath, "utf8")
        .replace(/^"""[\s\S]*?"""\n/, "")
        .replace(/^__all__ = \[[\s\S]*?\]\n/m, "")
        .replace(/^from __future__ import annotations\n/m, "")
        .trim();

      helpers.push(
        [
          "",
          `# --- inlined from ${relative(repoRoot, modulePath).replace(/\\/g, "/")} ---`,
          "",
          helper,
          "",
          "# --- end of inlined helpers ---",
        ].join("\n"),
      );
      return false;
    })
    .join("\n");

  // The policy's own module docstring stays at the top, then the helpers, then
  // the rest — otherwise the file would not start with its docstring.
  const docstring = /^"""[\s\S]*?"""/.exec(body);
  const head = docstring === null ? "" : docstring[0];
  const rest = docstring === null ? body : body.slice(head.length);

  // A future statement may only be preceded by the docstring, comments and
  // other future statements — so the policy's own copy cannot stay where it is
  // once inlined helper code sits above it. Strip it and emit exactly one.
  const future = "from __future__ import annotations";
  const withoutFuture = rest.replace(new RegExp(`^${future}\\n`, "m"), "");

  return [
    ...banner(policy, "#"),
    head,
    "",
    future,
    ...helpers,
    "",
    withoutFuture.trim(),
    "",
  ].join("\n");
}

// --- C --------------------------------------------------------------------------

/** Every `#include "policybook/..."` in a file, in the order written. */
function policybookIncludes(source: string): string[] {
  return [...source.matchAll(/^#include\s+"(policybook\/[^"]+)"/gm)].map((match) => match[1]!);
}

/** Every `#include <...>` in a file. */
function systemIncludes(source: string): string[] {
  return [...source.matchAll(/^#include\s+(<[^>]+>)/gm)].map((match) => match[1]!);
}

function stripIncludes(source: string): string {
  return source
    .split("\n")
    .filter((line) => !/^#include\s+["<]/.test(line.trim()))
    .join("\n")
    .trim();
}

/**
 * Collect a header and everything it includes, deepest first.
 *
 * Depth-first post-order, so a header is emitted only after the headers it
 * depends on — which is what makes concatenating them compile.
 */
function collectHeaders(
  header: string,
  includeRoot: string,
  seen: Set<string>,
  ordered: string[],
): void {
  if (seen.has(header)) return;
  seen.add(header);

  const path = join(includeRoot, header.replace(/^policybook\//, ""));
  if (!existsSync(path)) return;

  for (const nested of policybookIncludes(readFileSync(path, "utf8"))) {
    collectHeaders(nested, includeRoot, seen, ordered);
  }
  ordered.push(header);
}

export function inlineC(
  policy: DiscoveredPolicy,
  repoRoot: string,
): { header: string; source: string } {
  const base = identifier(policy.name);
  const includeRoot = join(repoRoot, "packages", "c", "include", "policybook");
  const srcRoot = join(repoRoot, "packages", "c", "src");

  const policyHeader = readFileSync(join(policy.dir, "policy.h"), "utf8");
  const policySource = readFileSync(join(policy.dir, "policy.c"), "utf8");

  // Every registry header the policy needs, transitively, in dependency order.
  const seen = new Set<string>();
  const ordered: string[] = [];
  for (const include of [
    ...policybookIncludes(policyHeader),
    ...policybookIncludes(policySource),
  ]) {
    collectHeaders(include, includeRoot, seen, ordered);
  }

  // Drop the policy's own header: its contents become this file's public API.
  //
  // The domain goes through `identifier` for the same reason the policy name
  // does — `rate-limiter` is not a C identifier and the include path spells it
  // `rate_limiter`. Comparing against the raw domain silently failed to match,
  // which left the policy in its own dependency list and emitted its entire
  // implementation twice. `policybook add` then produced a file that would not
  // compile, which `fresh-project.sh` caught the moment it was given a policy
  // from a hyphenated domain.
  const ownHeader = `policybook/${identifier(policy.domain)}/${base}.h`;
  if (!ordered.includes(ownHeader)) {
    throw new Error(
      `${policy.id}: expected its own header at ${ownHeader}, which the include walk did not ` +
        `reach. Found: ${ordered.join(", ")}. A policy must include its own header.`,
    );
  }
  const dependencies = ordered.filter((header) => header !== ownHeader);

  const systemFromHeaders = new Set<string>();
  const headerParts: string[] = [];
  for (const header of dependencies) {
    const path = join(includeRoot, header.replace(/^policybook\//, ""));
    if (!existsSync(path)) continue;
    const contents = readFileSync(path, "utf8");
    for (const include of systemIncludes(contents)) systemFromHeaders.add(include);
    headerParts.push(`/* --- inlined from ${header} --- */\n\n${stripIncludes(contents)}`);
  }
  for (const include of systemIncludes(policyHeader)) systemFromHeaders.add(include);

  const header = [
    ...banner(policy, "*"),
    `#ifndef POLICYBOOK_INLINE_${base.toUpperCase()}_H`,
    `#define POLICYBOOK_INLINE_${base.toUpperCase()}_H`,
    "",
    [...systemFromHeaders].sort().map((include) => `#include ${include}`).join("\n"),
    "",
    headerParts.join("\n\n"),
    "",
    "/* --- the policy itself --- */",
    "",
    stripIncludes(policyHeader),
    "",
    `#endif /* POLICYBOOK_INLINE_${base.toUpperCase()}_H */`,
    "",
  ].join("\n");

  // Implementations, in the same dependency order as their headers.
  const systemFromSources = new Set<string>();
  const sourceParts: string[] = [];
  for (const dependency of dependencies) {
    const implementation = join(srcRoot, dependency.replace(/^policybook\//, "").replace(/\.h$/, ".c"));
    if (!existsSync(implementation)) continue; // header-only, nothing to carry
    const contents = readFileSync(implementation, "utf8");
    for (const include of systemIncludes(contents)) systemFromSources.add(include);
    sourceParts.push(
      `/* --- inlined from ${dependency.replace(/\.h$/, ".c")} --- */\n\n${stripIncludes(contents)}`,
    );
  }
  for (const include of systemIncludes(policySource)) systemFromSources.add(include);

  const source = [
    ...banner(policy, "*"),
    `#include "${base}.h"`,
    "",
    [...systemFromSources].sort().map((include) => `#include ${include}`).join("\n"),
    "",
    sourceParts.join("\n\n"),
    "",
    "/* --- the policy itself --- */",
    "",
    stripIncludes(policySource),
    "",
  ].join("\n");

  return { header, source };
}
