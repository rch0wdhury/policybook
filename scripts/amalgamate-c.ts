/**
 * Flattens the C library into one STB-style header, `packages/c/dist/policybook.h`.
 *
 * The point is that a reader can take one file. No build system, no include
 * path, no submodule — drop it in, `#define POLICYBOOK_IMPLEMENTATION` in
 * exactly one translation unit, and every policy in the registry is available
 *.
 *
 *     #define POLICYBOOK_IMPLEMENTATION
 *     #include "policybook.h"
 *
 * How it works: the headers are emitted first, in dependency order, with their
 * `#include "policybook/..."` lines removed because the target is already
 * above them. Then the sources, wrapped in the implementation guard. Every
 * system include found anywhere is hoisted to the top and emitted once.
 *
 * **The collision check is the part worth knowing about.** All 41 sources land
 * in one translation unit, where two `static` functions sharing a name is a
 * redefinition error rather than two private helpers. Seven policies had a
 * `slot_for` when this script was first written. They are prefixed now, and
 * this refuses to write a header that would not compile — the failure names
 * every clash, so it is fixed at the source rather than debugged out of a
 * 20,000-line generated file.
 *
 * Usage: pnpm tsx scripts/amalgamate-c.ts
 */

import { mkdirSync, readFileSync, readdirSync, statSync, writeFileSync } from "node:fs";
import { dirname, join, relative } from "node:path";
import { fileURLToPath } from "node:url";
import { CORE_VERSION } from "../packages/core/src/index";

const HERE = dirname(fileURLToPath(import.meta.url));
const C_ROOT = join(HERE, "..", "packages", "c");
const INCLUDE_ROOT = join(C_ROOT, "include");
const SRC_ROOT = join(C_ROOT, "src");
const OUT = join(C_ROOT, "dist", "policybook.h");

/** Every file under `root` with the given extension, sorted for stability. */
function walk(root: string, extension: string): string[] {
  const found: string[] = [];
  const visit = (dir: string): void => {
    for (const entry of readdirSync(dir).sort()) {
      const path = join(dir, entry);
      if (statSync(path).isDirectory()) visit(path);
      else if (entry.endsWith(extension)) found.push(path);
    }
  };
  visit(root);
  return found;
}

const LOCAL_INCLUDE = /^\s*#\s*include\s+"([^"]+)"/;
const SYSTEM_INCLUDE = /^\s*#\s*include\s+<([^>]+)>/;

/** The `policybook/...` headers a file includes, as include-root-relative paths. */
function localIncludes(text: string): string[] {
  return text
    .split("\n")
    .map((line) => LOCAL_INCLUDE.exec(line)?.[1])
    .filter((name): name is string => name !== undefined);
}

/** The `<...>` headers a file includes. */
function systemIncludes(text: string): string[] {
  return text
    .split("\n")
    .map((line) => SYSTEM_INCLUDE.exec(line)?.[1])
    .filter((name): name is string => name !== undefined);
}

/** Drops every `#include` line; the content is being inlined around it. */
function stripIncludes(text: string): string {
  return text
    .split("\n")
    .filter((line) => !LOCAL_INCLUDE.test(line) && !SYSTEM_INCLUDE.test(line))
    .join("\n");
}

/**
 * Static function and object names a source defines at file scope.
 *
 * Deliberately a simple line-anchored match rather than a C parser: it is used
 * only to find collisions, and anything it misses is caught immediately
 * afterwards by the compiler on the generated header. Over-reporting would be
 * the harmful direction, and a line starting with `static` at column zero
 * inside a function body is not something this codebase does.
 */
function staticNames(text: string): string[] {
  const names: string[] = [];
  for (const line of text.split("\n")) {
    if (!line.startsWith("static ")) continue;
    // The identifier before `(` for a function, or before `[`, `=` or `;` for
    // an object. Either way it is the last identifier on the declaring line.
    const match = /\b([A-Za-z_][A-Za-z0-9_]*)\s*(\(|\[|=|;)/.exec(line.slice("static ".length));
    if (match?.[1] !== undefined) names.push(match[1]);
  }
  return names;
}

/** Header paths in an order where every include comes before its includer. */
function orderHeaders(headers: string[]): string[] {
  const byKey = new Map<string, string>();
  for (const path of headers) {
    byKey.set(relative(INCLUDE_ROOT, path).split("\\").join("/"), path);
  }

  const ordered: string[] = [];
  const state = new Map<string, "visiting" | "done">();

  const visit = (key: string, chain: string[]): void => {
    const status = state.get(key);
    if (status === "done") return;
    if (status === "visiting") {
      throw new Error(`include cycle: ${[...chain, key].join(" -> ")}`);
    }
    const path = byKey.get(key);
    if (path === undefined) return; // not one of ours

    state.set(key, "visiting");
    for (const dependency of localIncludes(readFileSync(path, "utf8"))) {
      visit(dependency, [...chain, key]);
    }
    state.set(key, "done");
    ordered.push(path);
  };

  for (const key of [...byKey.keys()].sort()) visit(key, []);
  return ordered;
}

function banner(headerCount: number, sourceCount: number): string[] {
  return [
    "/*",
    " * policybook — runnable decision policies, as one file.",
    " *",
    ` * Version ${CORE_VERSION}. GENERATED — do not edit. Regenerate with:`,
    " *",
    " *     pnpm tsx scripts/amalgamate-c.ts",
    " *",
    ` * ${headerCount} headers and ${sourceCount} implementation files, flattened.`,
    " * The originals are under packages/c/ and are what you should read; this is",
    " * for dropping into a project that would rather not have a build system.",
    " *",
    " * Use it like any STB-style header — the declarations come out of every",
    " * include, the implementation out of exactly one:",
    " *",
    " *     #define POLICYBOOK_IMPLEMENTATION",
    ' *     #include "policybook.h"',
    " *",
    " *     pb_cache_sieve_params params = PB_CACHE_SIEVE_PARAMS_DEFAULT;",
    " *     params.capacity = 1024u;",
    " *     pb_cache *cache = pb_cache_sieve.create(&params, NULL, NULL);",
    " *",
    " * Every policy takes all its memory in create and none afterwards, so it is",
    " * safe on a hot path. Nothing here reads a clock, a file, or an environment",
    " * variable: time and randomness are always supplied by the caller.",
    " *",
    " * Compile the implementation with -ffp-contract=off. Fusing a multiply and",
    " * an add into one rounding would make a policy's decisions differ from the",
    " * TypeScript and Python implementations on the same input, which is the one",
    " * property this registry exists to guarantee.",
    " *",
    " * MIT licensed.",
    " */",
    "",
  ];
}

function main(): void {
  const headers = orderHeaders(walk(INCLUDE_ROOT, ".h"));
  const sources = walk(SRC_ROOT, ".c");

  // Refuse to emit a header that cannot compile. See the note at the top.
  const owners = new Map<string, string[]>();
  for (const path of sources) {
    const where = relative(C_ROOT, path).split("\\").join("/");
    for (const name of staticNames(readFileSync(path, "utf8"))) {
      owners.set(name, [...(owners.get(name) ?? []), where]);
    }
  }
  const clashes = [...owners.entries()]
    .filter(([, files]) => new Set(files).size > 1)
    .map(([name, files]) => `  ${name}: ${[...new Set(files)].join(", ")}`);
  if (clashes.length > 0) {
    throw new Error(
      "amalgamate-c: these static names are defined in more than one source, and\n" +
        "all sources share one translation unit in the amalgamation:\n" +
        `${clashes.join("\n")}\n` +
        "Give each a name unique across the library — prefixing with the policy's\n" +
        "own name is what the rest of them do, and it reads better in a profiler.",
    );
  }

  // Every system header anyone asks for, once, at the top.
  const system = new Set<string>();
  for (const path of [...headers, ...sources]) {
    for (const name of systemIncludes(readFileSync(path, "utf8"))) system.add(name);
  }

  const out: string[] = [
    ...banner(headers.length, sources.length),
    "#ifndef POLICYBOOK_AMALGAMATED_H",
    "#define POLICYBOOK_AMALGAMATED_H",
    "",
    "/* Every system header the library uses, hoisted and de-duplicated. */",
    ...[...system].sort().map((name) => `#include <${name}>`),
    "",
  ];

  for (const path of headers) {
    const where = relative(C_ROOT, path).split("\\").join("/");
    out.push(
      `/* ${"=".repeat(72)}`,
      ` * ${where}`,
      ` * ${"=".repeat(72) } */`,
      "",
      stripIncludes(readFileSync(path, "utf8")).trim(),
      "",
    );
  }

  out.push(
    "#endif /* POLICYBOOK_AMALGAMATED_H */",
    "",
    "/*",
    " * The implementation. Define POLICYBOOK_IMPLEMENTATION in exactly one",
    " * translation unit before including this file; every other include gets the",
    " * declarations above and nothing else.",
    " */",
    "#ifdef POLICYBOOK_IMPLEMENTATION",
    "#ifndef POLICYBOOK_AMALGAMATED_IMPLEMENTATION",
    "#define POLICYBOOK_AMALGAMATED_IMPLEMENTATION",
    "",
  );

  for (const path of sources) {
    const where = relative(C_ROOT, path).split("\\").join("/");
    out.push(
      `/* ${"=".repeat(72)}`,
      ` * ${where}`,
      ` * ${"=".repeat(72)} */`,
      "",
      stripIncludes(readFileSync(path, "utf8")).trim(),
      "",
    );
  }

  out.push("#endif /* POLICYBOOK_AMALGAMATED_IMPLEMENTATION */", "#endif /* POLICYBOOK_IMPLEMENTATION */", "");

  const contents = out.join("\n");
  mkdirSync(dirname(OUT), { recursive: true });
  writeFileSync(OUT, contents);

  // Lines in the file, not entries in the array — most entries are whole files.
  const lines = contents.split("\n").length;
  console.log(
    `amalgamate-c: wrote packages/c/dist/policybook.h — ${headers.length} headers, ` +
      `${sources.length} sources, ${lines.toLocaleString()} lines`,
  );
}

main();
