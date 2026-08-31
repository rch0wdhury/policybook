/**
 * Copies each policy's C implementation into the C package.
 *
 * Policies live one-per-directory as `policy.c` / `policy.h`, which is right for
 * browsing the registry but wrong for a C library: every file would be called
 * `policy.h`. So the library tree is assembled — `policies/cache/lru/policy.h`
 * becomes `packages/c/include/policybook/cache/lru.h` — which is what lets a
 * user write `#include <policybook/cache/lru.h>` and what lets the generated
 * vector tests compile.
 *
 * The copies are committed, so the C tree is self-contained. They carry a
 * generated header and must never be edited directly; CI regenerates and
 * fails on a diff.
 *
 * Usage: pnpm tsx scripts/assemble-c.ts
 */

import { existsSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { discoverPolicies, findRepoRoot } from "../packages/vectors/src/discover";
import type { DiscoveredPolicy } from "../packages/vectors/src/types";

/** A C identifier fragment: `w-tinylfu` → `w_tinylfu`. */
function identifier(name: string): string {
  return name.replace(/-/g, "_");
}

/**
 * Rewrites a policy's source for its assembled location.
 *
 * In the registry a policy includes its own header as `policybook/cache/<name>.h`
 * already, so only the banner is added.
 */
function withBanner(source: string, policy: DiscoveredPolicy, original: string): string {
  const banner = [
    "/*",
    ` * GENERATED COPY — do not edit. Edit policies/${policy.id}/${original} instead,`,
    " * then run: pnpm tsx scripts/assemble-c.ts",
    " */",
    "",
  ].join("\n");
  return `${banner}${source}`;
}

function main(): void {
  const repoRoot = findRepoRoot();
  const policies = discoverPolicies(repoRoot).filter((policy) => policy.meta.ports?.includes("c"));

  const srcRoot = join(repoRoot, "packages", "c", "src");
  const includeRoot = join(repoRoot, "packages", "c", "include", "policybook");

  const expected = new Map<string, string>();
  let written = 0;

  for (const policy of policies) {
    const base = identifier(policy.name);
    const sourcePath = join(policy.dir, "policy.c");
    const headerPath = join(policy.dir, "policy.h");

    if (!existsSync(sourcePath) || !existsSync(headerPath)) {
      throw new Error(
        `${policy.id} declares a c port but is missing policy.c or policy.h`,
      );
    }

    // A domain name is not always a C identifier: `rate-limiter` becomes
    // `rate_limiter`, so the directory and the include path a user types are
    // both spellable in C.
    const domainDir = identifier(policy.domain);
    const targetSource = join(srcRoot, domainDir, `${base}.c`);
    const targetHeader = join(includeRoot, domainDir, `${base}.h`);
    expected.set(targetSource, "c");
    expected.set(targetHeader, "h");

    mkdirSync(join(srcRoot, domainDir), { recursive: true });
    mkdirSync(join(includeRoot, domainDir), { recursive: true });

    for (const [from, to, original] of [
      [sourcePath, targetSource, "policy.c"],
      [headerPath, targetHeader, "policy.h"],
    ] as const) {
      const assembled = withBanner(readFileSync(from, "utf8"), policy, original);
      const existing = existsSync(to) ? readFileSync(to, "utf8") : null;
      if (existing !== assembled) {
        writeFileSync(to, assembled);
        written += 1;
      }
    }
  }

  // Remove copies of policies that have left the registry, so the library
  // cannot keep exporting something the catalog no longer describes.
  const domains = new Set(policies.map((policy) => identifier(policy.domain)));
  for (const domain of domains) {
    for (const [root, extension] of [
      [join(srcRoot, domain), ".c"],
      [join(includeRoot, domain), ".h"],
    ] as const) {
      if (!existsSync(root)) continue;
      for (const entry of readdirSync(root)) {
        if (!entry.endsWith(extension)) continue;
        // traces.c / traces.h and the domain's own header are not policies.
        const path = join(root, entry);
        if (expected.has(path)) continue;
        if (entry === "traces.c" || entry === "traces.h" || entry === `${domain}.h`) continue;
        rmSync(path);
        console.log(`  removed stale ${entry}`);
      }
    }
  }

  // The umbrella header, so a caller can include one file and get everything.
  const domainList = [...new Set(policies.map((policy) => policy.domain))].sort();
  const umbrella: string[] = [
    "/*",
    " * GENERATED — do not edit. Regenerate with:",
    " *     pnpm tsx scripts/assemble-c.ts",
    " *",
    " * The umbrella header: everything libpolicybook exports, in one include.",
    " * Prefer the specific headers in a build you care about the size of.",
    " */",
    "",
    "#ifndef POLICYBOOK_H",
    "#define POLICYBOOK_H",
    "",
    "/* Core. */",
    '#include "policybook/allocator.h"',
    '#include "policybook/hash.h"',
    '#include "policybook/rng.h"',
    '#include "policybook/zipf.h"',
    "",
    "/* Data structures. */",
    '#include "policybook/ds/heap.h"',
    '#include "policybook/ds/ilist.h"',
    '#include "policybook/ds/map.h"',
    '#include "policybook/ds/ring.h"',
    "",
  ];

  for (const domain of domainList) {
    const domainDir = identifier(domain);
    umbrella.push(`/* Domain: ${domain}. */`);
    umbrella.push(`#include "policybook/${domainDir}/${domainDir}.h"`);
    umbrella.push(`#include "policybook/${domainDir}/traces.h"`);
    for (const policy of policies.filter((entry) => entry.domain === domain)) {
      umbrella.push(`#include "policybook/${domainDir}/${identifier(policy.name)}.h"`);
    }
    umbrella.push("");
  }

  umbrella.push("#endif /* POLICYBOOK_H */", "");

  const umbrellaPath = join(includeRoot, "policybook.h");
  const existingUmbrella = existsSync(umbrellaPath) ? readFileSync(umbrellaPath, "utf8") : null;
  const umbrellaText = umbrella.join("\n");
  if (existingUmbrella !== umbrellaText) {
    writeFileSync(umbrellaPath, umbrellaText);
    written += 1;
  }

  // The CMake side of the same list. CMake globbing would miss a policy added
  // without re-running configure, so the list is generated with the sources.
  const sources = policies
    .map((policy) => `    src/${identifier(policy.domain)}/${identifier(policy.name)}.c`)
    .sort();
  const manifest = [
    "# GENERATED by scripts/assemble-c.ts — do not edit.",
    "#",
    "# Policy implementations, assembled from policies/**/policy.c.",
    "",
    ...(sources.length > 0
      ? ["list(APPEND PB_POLICY_SOURCES", ...sources, ")"]
      : ["# (no C policies yet)"]),
    "",
  ].join("\n");

  const manifestPath = join(srcRoot, "policies.cmake");
  const existingManifest = existsSync(manifestPath) ? readFileSync(manifestPath, "utf8") : null;
  if (existingManifest !== manifest) {
    writeFileSync(manifestPath, manifest);
    written += 1;
  }

  console.log(
    `assemble-c — ${policies.length} C polic${policies.length === 1 ? "y" : "ies"}, ` +
      `${written} file(s) written.`,
  );
}

main();
