/**
 * Copies each policy's Python implementation into the `policybook` package.
 *
 * Policies live one-per-directory as `policy.py`, which is right for browsing
 * the registry and wrong for a Python package: every module would be called
 * `policy`. So the package is assembled — `policies/cache/lru/policy.py`
 * becomes `packages/python/policybook/domains/cache/lru.py` — which is what
 * lets a user write `from policybook.cache import Lru` (concept.md §12.1).
 *
 * It is also what lets the tree be type-checked at all: `mypy` refuses a set of
 * files that all share a module name, so `policies/**` can only be checked one
 * file at a time until it is assembled.
 *
 * The copies are committed and carry a generated header; CI regenerates and
 * fails on a diff.
 *
 * Usage: pnpm tsx scripts/assemble-python.ts
 */

import { existsSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { discoverPolicies, findRepoRoot } from "../packages/vectors/src/discover";
import type { DiscoveredPolicy } from "../packages/vectors/src/types";

/**
 * What each domain's package re-exports besides its policies.
 *
 * These modules are written by hand — the interface and the trace generators —
 * and the generated `__init__.py` has to carry them through.
 */
const DOMAIN_BASE_EXPORTS: Record<string, { module: string; names: string[] }[]> = {
  cache: [
    { module: "interface", names: ["CacheMeta", "CachePolicy"] },
    { module: "traces", names: ["CACHE_TRACES", "CacheTraceSpec", "generate_cache_trace"] },
  ],
  retry: [
    { module: "interface", names: ["RETRY_REFERENCE", "RetryError", "RetryPolicy"] },
    {
      module: "traces",
      names: [
        "RETRY_TRACES",
        "RetryTraceSpec",
        "environment_seed",
        "generate_retry_trace",
        "policy_seed",
      ],
    },
  ],
  "rate-limiter": [
    { module: "interface", names: ["RATE_LIMITER_REFERENCE", "RateLimiterPolicy"] },
    {
      module: "traces",
      names: [
        "RATE_LIMITER_TRACES",
        "RateLimiterTrace",
        "RateLimiterTraceSpec",
        "generate_rate_limiter_trace",
      ],
    },
  ],
  "kv-cache": [
    { module: "interface", names: ["KV_CACHE_BUDGETS", "KvCachePolicy"] },
    {
      module: "traces",
      names: [
        "KV_CACHE_TRACES",
        "KvCacheTraceSpec",
        "float32_bits",
        "fround",
        "generate_kv_cache_trace",
        "hash_kv_cache_trace",
      ],
    },
  ],
};

/** A Python identifier from a domain name: `kv-cache` → `kv_cache`. */
function moduleName(name: string): string {
  return name.replace(/-/g, "_");
}

/**
 * Order names the way `ruff`'s RUF022 wants an `__all__`: constants first, then
 * classes, then functions, each alphabetically.
 */
function sortExports(names: string[]): string[] {
  const rank = (name: string): number => {
    if (/^[A-Z0-9_]+$/.test(name)) return 0;
    if (/^[A-Z]/.test(name)) return 1;
    return 2;
  };
  return [...names].sort(
    (left, right) => rank(left) - rank(right) || left.localeCompare(right),
  );
}

/** `WTinyLfu` → `w_tiny_lfu`, so every module name is a valid identifier. */
function snakeCase(className: string): string {
  return className
    .replace(/([a-z0-9])([A-Z])/g, "$1_$2")
    .replace(/([A-Z]+)([A-Z][a-z])/g, "$1_$2")
    .toLowerCase();
}

/**
 * The single public class a policy module defines.
 *
 * The same rule the Python vector runner uses: helper classes are
 * underscore-prefixed and ignored, and exactly one public class must remain, so
 * the export is never ambiguous.
 */
function publicClassOf(source: string, policy: DiscoveredPolicy): string {
  const matches = [...source.matchAll(/^class\s+([A-Za-z_][A-Za-z0-9_]*)/gm)]
    .map((match) => match[1]!)
    .filter((name) => !name.startsWith("_"));

  if (matches.length !== 1) {
    throw new Error(
      `${policy.id}/policy.py must define exactly one public class; found ` +
        `${matches.length === 0 ? "none" : matches.join(", ")}`,
    );
  }
  return matches[0]!;
}

function banner(policy: DiscoveredPolicy): string {
  return [
    `# GENERATED COPY — do not edit. Edit policies/${policy.id}/policy.py instead,`,
    "# then run: pnpm tsx scripts/assemble-python.ts",
    "",
    "",
  ].join("\n");
}

function main(): void {
  const repoRoot = findRepoRoot();
  const packageRoot = join(repoRoot, "packages", "python", "policybook");
  const domainsRoot = join(packageRoot, "domains");

  const policies = discoverPolicies(repoRoot).filter((policy) =>
    policy.meta.ports?.includes("python"),
  );

  const byDomain = new Map<string, { policy: DiscoveredPolicy; module: string; klass: string }[]>();
  let written = 0;

  // A domain's interface and traces ship as soon as they exist, before its
  // first policy does — the parity tests import them, and a domain directory
  // without an `__init__.py` is an undeclared namespace package that works by
  // accident. Seeding the map here is what makes an infra-only domain a real
  // package rather than a directory that happens to be importable.
  for (const domain of Object.keys(DOMAIN_BASE_EXPORTS)) {
    mkdirSync(join(domainsRoot, moduleName(domain)), { recursive: true });
    byDomain.set(domain, []);
  }

  const write = (path: string, contents: string): void => {
    const existing = existsSync(path) ? readFileSync(path, "utf8") : null;
    if (existing !== contents) {
      writeFileSync(path, contents);
      written += 1;
    }
  };

  for (const policy of policies) {
    const sourcePath = join(policy.dir, "policy.py");
    if (!existsSync(sourcePath)) {
      throw new Error(`${policy.id} declares a python port but has no policy.py`);
    }

    const source = readFileSync(sourcePath, "utf8");
    const klass = publicClassOf(source, policy);
    const module = snakeCase(klass);

    const domainDir = join(domainsRoot, moduleName(policy.domain));
    mkdirSync(domainDir, { recursive: true });
    write(join(domainDir, `${module}.py`), banner(policy) + source);

    const entries = byDomain.get(policy.domain) ?? [];
    entries.push({ policy, module, klass });
    byDomain.set(policy.domain, entries);
  }

  for (const [domain, entries] of byDomain) {
    entries.sort((left, right) => left.klass.localeCompare(right.klass));
    const domainModule = moduleName(domain);
    const domainDir = join(domainsRoot, domainModule);
    const base = DOMAIN_BASE_EXPORTS[domain] ?? [];

    // The domain package: interface, traces, and every policy in it.
    const initLines: string[] = [
      `"""The \`${domain}\` domain.`,
      "",
      "GENERATED — do not edit. Regenerate with:",
      "    pnpm tsx scripts/assemble-python.ts",
      '"""',
      "",
      "from __future__ import annotations",
      "",
    ];

    // Imports sorted by module and __all__ in isort's order, so the generated
    // files satisfy the same lint rules as the hand-written ones.
    const imports = [
      ...base.map((group) => ({ module: group.module, names: group.names })),
      ...entries.map((entry) => ({ module: entry.module, names: [entry.klass] })),
    ].sort((left, right) => left.module.localeCompare(right.module));

    // ruff's line limit is 100, and a domain re-exporting four names from one
    // module passes it — `rate-limiter` was the first to find that out. Long
    // imports wrap into the parenthesised form ruff would produce itself.
    const LINE_LIMIT = 100;
    for (const group of imports) {
      const prefix = `from policybook.domains.${domainModule}.${group.module} import `;
      const single = `${prefix}${group.names.join(", ")}`;
      if (single.length <= LINE_LIMIT) {
        initLines.push(single);
        continue;
      }
      initLines.push(`${prefix}(`);
      for (const name of group.names) initLines.push(`    ${name},`);
      initLines.push(")");
    }

    const exported = sortExports([
      ...base.flatMap((group) => group.names),
      ...entries.map((entry) => entry.klass),
    ]);

    initLines.push("");
    initLines.push("__all__ = [");
    for (const name of exported) initLines.push(`    "${name}",`);
    initLines.push("]");
    initLines.push("");

    write(join(domainDir, "__init__.py"), initLines.join("\n"));

    // The public shortcut: `from policybook.cache import Lru` rather than
    // `from policybook.domains.cache import Lru` (concept.md §12.1).
    //
    // A domain with no policies yet still gets one, so its interface and traces
    // are importable by the documented path; the example names whatever it does
    // export.
    const headline =
      entries.length === 0
        ? "interface and traces only, no policies yet"
        : `${entries.length} polic${entries.length === 1 ? "y" : "ies"}`;
    const example = entries[0]?.klass ?? exported[0] ?? "";
    const aliasLines = [
      `"""The \`${domain}\` domain: ${headline}.`,
      "",
      `    from policybook.${domainModule} import ${example}`,
      "",
      "GENERATED — do not edit. Regenerate with:",
      "    pnpm tsx scripts/assemble-python.ts",
      '"""',
      "",
      "from __future__ import annotations",
      "",
      `from policybook.domains.${domainModule} import (`,
      ...exported.map((name) => `    ${name},`),
      ")",
      "",
      "__all__ = [",
      ...exported.map((name) => `    "${name}",`),
      "]",
      "",
    ];
    write(join(packageRoot, `${domainModule}.py`), aliasLines.join("\n"));
  }

  // Drop copies of policies that have left the registry, so the package cannot
  // keep exporting something the catalog no longer describes.
  for (const [domain, entries] of byDomain) {
    const domainDir = join(domainsRoot, moduleName(domain));
    const keep = new Set([
      "__init__.py",
      ...(DOMAIN_BASE_EXPORTS[domain] ?? []).map((group) => `${group.module}.py`),
      ...entries.map((entry) => `${entry.module}.py`),
    ]);
    for (const entry of readdirSync(domainDir)) {
      if (!entry.endsWith(".py") || keep.has(entry)) continue;
      rmSync(join(domainDir, entry));
      console.log(`  removed stale ${domain}/${entry}`);
    }
  }

  console.log(
    `assemble-python — ${policies.length} polic${policies.length === 1 ? "y" : "ies"}, ` +
      `${written} file(s) written.`,
  );
}

main();
