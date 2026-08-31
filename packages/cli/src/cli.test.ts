import { spawnSync } from "node:child_process";
import { existsSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { discoverPolicies, findRepoRoot } from "@policybook/vectors";
import type { DiscoveredPolicy } from "@policybook/vectors";
import { boolFlag, listFlag, parseArgs, stringFlag } from "./args";
import { padEnd, truncate, visibleWidth } from "./ansi";
import { inlineC, inlinePython, inlineTypeScript } from "./inline";
import { renderMarkdown } from "./markdown";
import { scaffoldPolicy } from "./scaffold";
import { renderTable } from "./table";

const BUILT = fileURLToPath(new URL("../dist/cli.js", import.meta.url));

/** Run the built CLI. Skipped when it has not been built. */
function run(args: string[]): { status: number | null; stdout: string; stderr: string } {
  const result = spawnSync(process.execPath, [BUILT, ...args, "--no-color"], {
    encoding: "utf8",
    cwd: fileURLToPath(new URL("../../..", import.meta.url)),
  });
  return { status: result.status, stdout: result.stdout, stderr: result.stderr };
}

describe("parseArgs", () => {
  it("separates the command from its positionals", () => {
    const args = parseArgs(["verify", "cache", "cache/lru"]);
    expect(args.command).toBe("verify");
    expect(args.positionals).toEqual(["cache", "cache/lru"]);
  });

  it("accepts --key=value and --key value for value flags", () => {
    expect(stringFlag(parseArgs(["verify", "--lang=ts"]), "lang")).toBe("ts");
    expect(stringFlag(parseArgs(["verify", "--lang", "ts"]), "lang")).toBe("ts");
  });

  it("does not swallow a positional after a boolean flag", () => {
    // `--all` takes no value, so `cache` stays a positional.
    const args = parseArgs(["verify", "--all", "cache"]);
    expect(boolFlag(args, "all")).toBe(true);
    expect(args.positionals).toEqual(["cache"]);
  });

  it("splits a comma-separated list flag", () => {
    expect(listFlag(parseArgs(["verify", "--lang", "ts, python ,c"]), "lang")).toEqual([
      "ts",
      "python",
      "c",
    ]);
    expect(listFlag(parseArgs(["verify"]), "lang")).toBeUndefined();
  });

  it("understands the conventional short flags", () => {
    expect(boolFlag(parseArgs(["-h"]), "help")).toBe(true);
    expect(boolFlag(parseArgs(["-v"]), "version")).toBe(true);
    expect(() => parseArgs(["-q"])).toThrow(/unknown option -q/);
  });

  it("ignores the bare -- that package managers forward", () => {
    const args = parseArgs(["bench", "--", "--lang", "ts"]);
    expect(args.command).toBe("bench");
    expect(stringFlag(args, "lang")).toBe("ts");
  });
});

describe("ansi helpers", () => {
  it("measures and pads on the visible width", () => {
    const coloured = "[1mbold[22m";
    expect(visibleWidth(coloured)).toBe(4);
    // Padding must account for the escapes, or columns drift.
    expect(visibleWidth(padEnd(coloured, 10))).toBe(10);
  });

  it("truncates with an ellipsis", () => {
    expect(truncate("abcdefgh", 5)).toBe("abcd…");
    expect(truncate("abc", 5)).toBe("abc");
  });
});

describe("renderTable", () => {
  it("renders headers and rows", () => {
    const output = renderTable(
      [
        { header: "ID", value: (row) => ["a", "bb"][row]! },
        { header: "NAME", value: (row) => ["one", "two"][row]! },
      ],
      2,
    );
    expect(output).toContain("ID");
    expect(output).toContain("bb");
    expect(output.split("\n")).toHaveLength(3);
  });

  it("says so when there is nothing to show", () => {
    expect(renderTable([{ header: "ID", value: () => "" }], 0)).toContain("nothing to show");
  });
});

describe("renderMarkdown", () => {
  it("keeps the text and drops the syntax", () => {
    const output = renderMarkdown("# Title\n\nSome **bold** and `code`.\n\n- a bullet\n");
    expect(output).toContain("Title");
    expect(output).toContain("bold");
    expect(output).toContain("code");
    expect(output).toContain("• a bullet");
    expect(output).not.toContain("**");
  });

  it("drops the generated benchmark markers", () => {
    const output = renderMarkdown("## Benchmark\n\n<!-- bench:start -->\nrows\n<!-- bench:end -->\n");
    expect(output).toContain("rows");
    expect(output).not.toContain("bench:start");
  });

  it("shows a link's text rather than its URL", () => {
    const output = renderMarkdown("See [SIEVE](../sieve/) for more.");
    expect(output).toContain("SIEVE");
    expect(output).not.toContain("../sieve/");
  });
});

describe.skipIf(!existsSync(BUILT))("the built CLI", () => {
  it("lists the catalog", () => {
    const result = run(["list"]);
    expect(result.status).toBe(0);
    expect(result.stdout).toContain("cache/sieve");
    expect(result.stdout).toContain("★");
  });

  it("filters by domain", () => {
    const result = run(["list", "cache"]);
    expect(result.status).toBe(0);
    expect(result.stdout).toContain("cache/lru");
  });

  it("emits machine-readable output on request", () => {
    const result = run(["list", "--json"]);
    expect(result.status).toBe(0);
    const parsed = JSON.parse(result.stdout) as { id: string }[];
    expect(parsed.length).toBeGreaterThan(0);
    expect(parsed.some((policy) => policy.id === "cache/sieve")).toBe(true);
  });

  it("shows a policy's page", () => {
    const result = run(["show", "cache/sieve"]);
    expect(result.status).toBe(0);
    expect(result.stdout).toContain("When not to use it");
  });

  it("suggests something when a policy is misspelled", () => {
    const result = run(["show", "sieve"]);
    expect(result.status).toBe(1);
    expect(result.stderr).toContain("cache/sieve");
  });

  it("validates the catalog", () => {
    const result = run(["check"]);
    expect(result.status).toBe(0);
    expect(result.stdout).toContain("all valid");
  });

  it("verifies vectors in TypeScript", () => {
    const result = run(["verify", "cache/fifo", "--lang", "ts"]);
    expect(result.status).toBe(0);
    expect(result.stdout).toContain("cache/fifo");
    expect(result.stdout).toContain("passed");
  });

  it("rejects an unknown language by name", () => {
    const result = run(["verify", "cache/fifo", "--lang", "rust"]);
    expect(result.status).toBe(1);
    expect(result.stderr).toContain("ts, python, c");
  });

  it("prints help for a single command", () => {
    const result = run(["verify", "--help"]);
    expect(result.status).toBe(0);
    expect(result.stdout).toContain("EXAMPLE");
    expect(result.stdout).toContain("vectors.json");
  });

  it("exits 2 on an unknown command", () => {
    expect(run(["nonsense"]).status).toBe(2);
  });

  it("starts quickly", () => {
    // `npx policybook add ...` should feel instant, which is the whole reason
    // the CLI has no runtime dependencies. The budget is 300ms; this asserts
    // three times that so a loaded machine cannot make it flaky, and the
    // measured figure is logged for anyone watching the trend.
    const started = performance.now();
    const result = run(["list"]);
    const elapsed = performance.now() - started;

    expect(result.status).toBe(0);
    console.log(`cli: \`policybook list\` in ${elapsed.toFixed(0)}ms (budget 300ms)`);
    expect(elapsed).toBeLessThan(900);
  });
});

// --- add: the copied file must stand on its own -------------------------------
//
// That the output *compiles* is checked for real, in empty directories outside
// the repository, by packages/cli/tests/fresh-project.sh. What is checked here
// is the shape of what the inliner emits, which is quicker to pin and quicker
// to read when it breaks.

const REPO = findRepoRoot();
const CATALOG = discoverPolicies(REPO);

function policy(id: string): DiscoveredPolicy {
  const found = CATALOG.find((entry) => entry.id === id);
  if (found === undefined) throw new Error(`the test fixture ${id} is no longer in the catalog`);
  return found;
}

// One policy that shares nothing, and one that takes mix32 from the core Rng.
const SIMPLE = "cache/sieve";
const DEPENDENT = "cache/w-tinylfu";

describe("inline", () => {
  it("keeps provenance at the top of every language", () => {
    const ts = inlineTypeScript(policy(SIMPLE), REPO);
    const py = inlinePython(policy(SIMPLE), REPO);
    const c = inlineC(policy(SIMPLE), REPO);

    for (const emitted of [ts, py, c.header, c.source]) {
      expect(emitted).toContain("Copied from the Policybook registry: cache/sieve");
      expect(emitted).toContain("policies/cache/sieve");
    }
  });

  it("leaves no import pointing back at the registry", () => {
    expect(inlineTypeScript(policy(DEPENDENT), REPO)).not.toMatch(/^import .* from "\.\./m);
    expect(inlinePython(policy(DEPENDENT), REPO)).not.toMatch(/^from policybook/m);

    const c = inlineC(policy(DEPENDENT), REPO);
    expect(c.header).not.toMatch(/^#include "policybook\//m);
    // The source may include its own emitted header, and nothing else local.
    expect(c.source).not.toMatch(/^#include "policybook\//m);
    expect(c.source).toContain('#include "w_tinylfu.h"');
  });

  it("carries the shared helper's code, not just its name", () => {
    // The reason `add` inlines whole modules rather than the one symbol used:
    // a partial copy can compile and still compute the wrong answer.
    expect(inlineTypeScript(policy(DEPENDENT), REPO)).toContain("function mix32");
    expect(inlinePython(policy(DEPENDENT), REPO)).toContain("def mix32");
    // The definition, not a call site: a header-only copy would compile here
    // and fail to link in someone else's project.
    expect(inlineC(policy(DEPENDENT), REPO).source).toMatch(/^uint32_t pb_mix32\(/m);
  });

  it("emits exactly one __future__ import, at the top", () => {
    // Python allows a future statement only above real code, so the policy's own
    // copy cannot survive once helper code is inlined above it.
    const py = inlinePython(policy(DEPENDENT), REPO);
    const futures = [...py.matchAll(/^from __future__ import annotations$/gm)];
    expect(futures).toHaveLength(1);

    const before = py.slice(0, futures[0]!.index);
    expect(before).not.toContain("def ");
    expect(before).not.toContain("class ");
  });

  it("guards the emitted C header against double inclusion", () => {
    const { header } = inlineC(policy(SIMPLE), REPO);
    expect(header).toContain("#ifndef POLICYBOOK_INLINE_SIEVE_H");
    expect(header).toContain("#endif");
  });

});

describe("scaffold", () => {
  const files = scaffoldPolicy("cache", "my-policy");
  const byPath = new Map(files.map((file) => [file.path, file.contents]));

  it("writes every file the catalog requires", () => {
    expect([...byPath.keys()].sort()).toEqual([
      "README.md",
      "index.ts",
      "policy.json",
      "vectors.gen.ts",
    ]);
  });

  it("derives a class name from the id", () => {
    expect(byPath.get("index.ts")).toContain("export default class MyPolicy<K>");
    expect(JSON.parse(byPath.get("policy.json")!)).toMatchObject({
      id: "cache/my-policy",
      name: "MyPolicy",
      domain: "cache",
    });
  });

  it("leaves a TODO everywhere `check` will demand something", () => {
    // The contributor should learn what is missing from `policybook check`,
    // not by reading the spec. Both README sections the validator insists on
    // are present but empty.
    const readme = byPath.get("README.md")!;
    expect(readme).toContain("## When to use it");
    expect(readme).toContain("## When not to use it");
    expect(readme).toContain("<!-- bench:start -->");
    for (const contents of byPath.values()) expect(contents).toContain("TODO");
  });

  it("scaffolds the four vector cases the catalog asks for", () => {
    const vectors = byPath.get("vectors.gen.ts")!;
    for (const kind of ["smoke:", "boundary:", "distinguishing:", "tiebreak:"]) {
      expect(vectors).toContain(kind);
    }
  });

});

// These spawn dist/cli.js, so they live behind the same guard as "the built
// CLI" above — outside it they failed on any tree that had never run the
// build, which is what a fresh CI checkout is.
describe.skipIf(!existsSync(BUILT))("the built CLI, on refusals", () => {
  it("refuses to copy an offline bound", () => {
    // OPT needs the whole future access sequence. Copying it into a project
    // would hand someone a policy that cannot run online, so `add` says so.
    const result = run(["add", "cache/opt"]);
    expect(result.status).toBe(1);
    expect(result.stderr).toContain("offline bound");
  });

  it("names the languages it knows when given another", () => {
    const result = run(["add", SIMPLE, "--lang", "rust"]);
    expect(result.status).toBe(1);
    expect(result.stderr).toContain("ts, python, c");
  });

  it("suggests the catalog when the id is unknown", () => {
    const result = run(["add", "cache/nope"]);
    expect(result.status).toBe(1);
    expect(result.stderr).toContain("cache/nope");
  });

  it("rejects a scaffold id it could not create a directory for", () => {
    expect(run(["new", "nodomain"]).status).toBe(1);
    expect(run(["new", "cache/Not Valid"]).status).toBe(1);
    // Refusing to overwrite matters most: this is someone's work.
    const existing = run(["new", "cache/lru"]);
    expect(existing.status).toBe(1);
    expect(existing.stderr).toContain("already exists");
  });
});
