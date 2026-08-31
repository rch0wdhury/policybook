/**
 * The commands themselves.
 *
 * Every one prints something a person can act on: `list` gives the decision at
 * a glance, `show` gives the page, `check` names the file and the rule, and
 * `verify` says which language disagreed and where.
 */

import { spawnSync } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import {
  checkCatalog,
  discoverPolicies,
  findRepoRoot,
  formatFailures,
  loadFactory,
  loadVectors,
  reportCatalog,
  runVectors,
} from "@policybook/vectors";
import type { DiscoveredPolicy } from "@policybook/vectors";
import { bold, cyan, dim, green, red, yellow } from "./ansi";
import { inlineC, inlinePython, inlineTypeScript } from "./inline";
import { renderMarkdown } from "./markdown";
import { scaffoldPolicy } from "./scaffold";
import { renderTable } from "./table";
import type { ParsedArgs } from "./args";
import { boolFlag, listFlag, stringFlag } from "./args";

/** Languages `verify` knows how to run. */
const LANGUAGES = ["ts", "python", "c"] as const;
type Language = (typeof LANGUAGES)[number];

/**
 * Where the registry lives.
 *
 * Two ways to arrive here. Inside a checkout, the working directory is under the
 * repository and that is the registry to read. But `policybook add` is meant to
 * be run from *your* project — the whole point is copying a policy somewhere
 * else — so when the working directory is not a checkout, fall back to walking
 * up from the CLI's own location. That covers a cloned repo whose `bin` is on
 * PATH, and it is why the fresh-project tests can run in an empty tmp dir.
 */
function requireRepo(): string {
  try {
    return findRepoRoot(process.cwd());
  } catch {
    // Nothing above the working directory; try the package's own copy.
  }
  // An npm install carries the registry inside the package, next to dist/ —
  // `prepack` copies it there, and this is what makes `npx policybook add`
  // work outside a checkout.
  const packaged = fileURLToPath(new URL("..", import.meta.url));
  if (existsSync(join(packaged, "policies"))) return packaged;
  try {
    return findRepoRoot();
  } catch {
    throw new Error(
      "could not find the policybook registry.\n" +
        "These commands read policies from the repository. Clone it, or see\n" +
        "https://github.com/rch0wdhury/policybook for the browsable catalog.",
    );
  }
}

/** Resolve a target to policies: an id, a domain, or everything. */
function select(all: DiscoveredPolicy[], targets: string[], everything: boolean): DiscoveredPolicy[] {
  if (everything || targets.length === 0) return all;

  const selected: DiscoveredPolicy[] = [];
  for (const target of targets) {
    const matches = all.filter((policy) => policy.id === target || policy.domain === target);
    if (matches.length === 0) {
      const known = [...new Set(all.map((policy) => policy.domain))].join(", ");
      throw new Error(
        `no policy or domain called "${target}".\n` +
          `Known domains: ${known}. Run \`policybook list\` to see everything.`,
      );
    }
    for (const match of matches) if (!selected.includes(match)) selected.push(match);
  }
  return selected;
}

// --- list --------------------------------------------------------------------

export function commandList(args: ParsedArgs): number {
  const all = discoverPolicies(requireRepo());
  const policies = select(all, args.positionals, false);

  if (boolFlag(args, "json")) {
    console.log(JSON.stringify(policies.map((policy) => policy.meta), null, 2));
    return 0;
  }

  const rows = [...policies].sort((left, right) => left.id.localeCompare(right.id));

  console.log(
    renderTable(
      [
        {
          header: "",
          value: (row) => (rows[row]!.meta.recommended ? yellow("★") : " "),
        },
        { header: "POLICY", value: (row) => cyan(rows[row]!.id) },
        { header: "NAME", value: (row) => rows[row]!.meta.name },
        {
          header: "SUMMARY",
          value: (row) => rows[row]!.meta.summary,
          flexible: true,
          minWidth: 30,
        },
      ],
      rows.length,
    ),
  );

  const recommended = rows.filter((policy) => policy.meta.recommended).length;
  console.log(
    "\n" +
      dim(
        `${rows.length} polic${rows.length === 1 ? "y" : "ies"}` +
          (recommended > 0 ? `, ${yellow("★")}${dim(" marks a recommended default")}` : "") +
          `. \`policybook show <id>\` for the full page.`,
      ),
  );
  return 0;
}

// --- show --------------------------------------------------------------------

export function commandShow(args: ParsedArgs): number {
  const target = args.positionals[0];
  if (target === undefined) {
    throw new Error("show needs a policy id.\nExample: policybook show cache/sieve");
  }

  const all = discoverPolicies(requireRepo());
  const policy = all.find((entry) => entry.id === target);
  if (policy === undefined) {
    const suggestions = all
      .filter((entry) => entry.name.includes(target) || entry.id.includes(target))
      .map((entry) => entry.id);
    throw new Error(
      `no policy called "${target}".` +
        (suggestions.length > 0 ? `\nDid you mean: ${suggestions.join(", ")}?` : "") +
        "\nRun `policybook list` to see everything.",
    );
  }

  const readme = join(policy.dir, "README.md");
  if (!existsSync(readme)) throw new Error(`${policy.id} has no README`);
  console.log(renderMarkdown(readFileSync(readme, "utf8")));
  return 0;
}

// --- check -------------------------------------------------------------------

export function commandCheck(): number {
  return reportCatalog(checkCatalog(requireRepo()));
}

// --- verify ------------------------------------------------------------------

interface VerifyOutcome {
  language: Language;
  policy: string;
  ok: boolean;
  detail: string;
}

async function verifyTypeScript(policy: DiscoveredPolicy): Promise<VerifyOutcome> {
  const base = { language: "ts" as const, policy: policy.id };
  if (!existsSync(policy.vectorsPath)) {
    return { ...base, ok: false, detail: "no vectors.json" };
  }

  let factory;
  try {
    factory = await loadFactory(policy.entry);
  } catch (error) {
    // The policy sources are TypeScript, run directly via Node's type
    // stripping — unflagged only from Node 22.18.
    if (error instanceof Error && "code" in error && error.code === "ERR_UNKNOWN_FILE_EXTENSION") {
      return {
        ...base,
        ok: false,
        detail:
          `verify --lang ts runs the .ts sources directly, which needs Node >= 22.18 ` +
          `(type stripping); this is ${process.version}`,
      };
    }
    throw error;
  }
  const result = runVectors(factory, loadVectors(policy.vectorsPath));
  return result.failures.length === 0
    ? { ...base, ok: true, detail: `${result.assertionsRun} assertions` }
    : { ...base, ok: false, detail: formatFailures(result) };
}

/**
 * Find a Python that has the package importable.
 *
 * A virtual environment's `python` usually has it while the system `python3`
 * does not, so `python` is tried first, and the failure says how to fix it.
 */
function pythonCandidates(args: ParsedArgs): string[] {
  const explicit = stringFlag(args, "python") ?? process.env["POLICYBOOK_PYTHON"];
  return explicit !== undefined ? [explicit] : ["python", "python3"];
}

function verifyPython(policy: DiscoveredPolicy, args: ParsedArgs): VerifyOutcome {
  const base = { language: "python" as const, policy: policy.id };
  if (!policy.meta.ports.includes("python")) {
    return { ...base, ok: true, detail: "no python port declared, skipped" };
  }

  for (const executable of pythonCandidates(args)) {
    const run = spawnSync(executable, ["-m", "policybook._vectors", policy.dir], {
      encoding: "utf8",
    });
    if (run.error !== undefined) continue; // not on PATH, try the next
    if (run.status === 0) {
      return { ...base, ok: true, detail: (run.stdout || "").trim() };
    }
    // A missing package is a setup problem, not a policy failure.
    if ((run.stderr || "").includes("No module named policybook")) continue;
    return { ...base, ok: false, detail: `${run.stdout || ""}${run.stderr || ""}`.trim() };
  }

  return {
    ...base,
    ok: false,
    detail:
      "no Python with the policybook package importable.\n" +
      "  Install it with `uv pip install -e packages/python`, or point at one\n" +
      "  with --python <executable> or POLICYBOOK_PYTHON.",
  };
}

function verifyC(policies: DiscoveredPolicy[], args: ParsedArgs, repoRoot: string): VerifyOutcome[] {
  const wanted = policies.filter((policy) => policy.meta.ports.includes("c"));
  const skipped = policies
    .filter((policy) => !policy.meta.ports.includes("c"))
    .map((policy) => ({
      language: "c" as const,
      policy: policy.id,
      ok: true,
      detail: "no c port declared, skipped",
    }));

  if (wanted.length === 0) return skipped;

  const buildDir = stringFlag(args, "c-build") ?? join(repoRoot, ".cache", "c-verify");
  const failure = (detail: string): VerifyOutcome[] => [
    ...skipped,
    ...wanted.map((policy) => ({
      language: "c" as const,
      policy: policy.id,
      ok: false,
      detail,
    })),
  ];

  const configure = spawnSync(
    "cmake",
    ["-S", join(repoRoot, "packages", "c"), "-B", buildDir, "-DCMAKE_BUILD_TYPE=Debug"],
    { encoding: "utf8" },
  );
  if (configure.error !== undefined) {
    return failure(
      "cmake is not on PATH. The C port needs a toolchain — on Windows, run this\n" +
        "  inside WSL. See packages/c/README or CONTRIBUTING.md.",
    );
  }
  if (configure.status !== 0) {
    return failure(`cmake configure failed:\n${configure.stderr || configure.stdout || ""}`.trim());
  }

  const build = spawnSync("cmake", ["--build", buildDir], { encoding: "utf8" });
  if (build.status !== 0) {
    return failure(`build failed:\n${(build.stderr || build.stdout || "").trim()}`);
  }

  return [
    ...skipped,
    ...wanted.map((policy) => {
      // The whole id is underscored, domain included — the registered ctest
      // name for kv-cache/h2o is kv_cache_h2o_vectors, and mapping only the
      // name once reported success for 14 policies while running nothing.
      // --no-tests=error keeps that class of bug loud: a pattern matching
      // zero tests is a failure, never a green.
      const test = `${policy.id.replace(/[-/]/g, "_")}_vectors`;
      const run = spawnSync(
        "ctest",
        ["--test-dir", buildDir, "-R", `^${test}$`, "--no-tests=error", "--output-on-failure"],
        { encoding: "utf8" },
      );
      return {
        language: "c" as const,
        policy: policy.id,
        ok: run.status === 0,
        detail:
          run.status === 0 ? "vectors green" : (run.stdout || run.stderr || "").trim(),
      };
    }),
  ];
}

export async function commandVerify(args: ParsedArgs): Promise<number> {
  const repoRoot = requireRepo();
  const all = discoverPolicies(repoRoot);
  const policies = select(all, args.positionals, boolFlag(args, "all"));

  const requested = listFlag(args, "lang") ?? [...LANGUAGES];
  for (const language of requested) {
    if (!LANGUAGES.includes(language as Language)) {
      throw new Error(`unknown language "${language}". Choose from ${LANGUAGES.join(", ")}.`);
    }
  }

  const outcomes: VerifyOutcome[] = [];
  for (const language of requested as Language[]) {
    if (language === "c") {
      outcomes.push(...verifyC(policies, args, repoRoot));
      continue;
    }
    for (const policy of policies) {
      outcomes.push(
        language === "ts" ? await verifyTypeScript(policy) : verifyPython(policy, args),
      );
    }
  }

  const failures = outcomes.filter((outcome) => !outcome.ok);
  for (const outcome of outcomes) {
    const mark = outcome.ok ? green("ok  ") : red("FAIL");
    console.log(`${mark} ${bold(outcome.policy)} ${dim(`(${outcome.language})`)}`);
    if (!outcome.ok) {
      for (const line of outcome.detail.split("\n")) console.log(`     ${line}`);
    }
  }

  console.log(
    "\n" +
      (failures.length === 0
        ? green(`${outcomes.length} check(s) passed.`)
        : red(`${failures.length} of ${outcomes.length} check(s) failed.`)),
  );
  return failures.length === 0 ? 0 : 1;
}

// --- new ---------------------------------------------------------------------

export function commandNew(args: ParsedArgs): number {
  const target = args.positionals[0];
  if (target === undefined || !target.includes("/")) {
    throw new Error(
      "new needs a domain and a name.\nExample: policybook new cache/my-policy",
    );
  }

  const [domain, name] = target.split("/", 2) as [string, string];
  if (!/^[a-z0-9-]+$/.test(domain) || !/^[a-z0-9-]+$/.test(name)) {
    throw new Error(
      `"${target}" is not a valid id. Use lowercase letters, digits and hyphens: cache/my-policy`,
    );
  }

  const repoRoot = requireRepo();
  const directory = join(repoRoot, "policies", domain, name);
  if (existsSync(directory)) {
    throw new Error(`${relative(repoRoot, directory)} already exists`);
  }

  mkdirSync(directory, { recursive: true });
  const files = scaffoldPolicy(domain, name);
  for (const file of files) {
    writeFileSync(join(directory, file.path), file.contents);
  }

  console.log(`${green("created")} ${bold(target)} in ${relative(repoRoot, directory)}`);
  for (const file of files) console.log(`  ${dim(file.path)}`);
  console.log(
    "\n" +
      [
        "Next:",
        `  1. Implement ${cyan("index.ts")}.`,
        `  2. Fill in ${cyan("vectors.gen.ts")} — every TODO is a case the catalog requires.`,
        `  3. ${cyan(`pnpm gen:vectors ${target}`)} to produce vectors.json.`,
        `  4. Write the README. ${cyan("policybook check")} will tell you what is missing.`,
      ].join("\n"),
  );
  return 0;
}

// --- add ---------------------------------------------------------------------

const ADD_LANGUAGES = ["ts", "python", "c"] as const;
type AddLanguage = (typeof ADD_LANGUAGES)[number];

export function commandAdd(args: ParsedArgs): number {
  const target = args.positionals[0];
  if (target === undefined) {
    throw new Error("add needs a policy id.\nExample: policybook add cache/sieve");
  }

  const repoRoot = requireRepo();
  const policy = discoverPolicies(repoRoot).find((entry) => entry.id === target);
  if (policy === undefined) {
    throw new Error(
      `no policy called "${target}". Run \`policybook list\` to see everything.`,
    );
  }

  const language = (stringFlag(args, "lang") ?? "ts") as AddLanguage;
  if (!ADD_LANGUAGES.includes(language)) {
    throw new Error(`unknown language "${language}". Choose from ${ADD_LANGUAGES.join(", ")}.`);
  }
  if (!policy.meta.ports.includes(language)) {
    throw new Error(
      `${policy.id} has no ${language} port. It ships: ${policy.meta.ports.join(", ")}.`,
    );
  }
  if (policy.meta.status === "offline-bound") {
    throw new Error(
      `${policy.id} is an offline bound: it needs the whole future access sequence, so it\n` +
        "cannot run in a real cache. It exists as the reference line in benchmarks.",
    );
  }

  const outDir = resolve(process.cwd(), stringFlag(args, "out") ?? ".");
  mkdirSync(outDir, { recursive: true });

  const base = policy.name.replace(/-/g, "_");
  const written: string[] = [];
  const write = (filename: string, contents: string): void => {
    writeFileSync(join(outDir, filename), contents);
    written.push(filename);
  };

  if (language === "ts") {
    write(`${policy.name}.ts`, inlineTypeScript(policy, repoRoot));
  } else if (language === "python") {
    write(`${base}.py`, inlinePython(policy, repoRoot));
  } else {
    const emitted = inlineC(policy, repoRoot);
    write(`${base}.h`, emitted.header);
    write(`${base}.c`, emitted.source);
  }

  console.log(`${green("added")} ${bold(policy.id)} ${dim(`(${language})`)}`);
  for (const filename of written) {
    console.log(`  ${join(relative(process.cwd(), outDir) || ".", filename)}`);
  }
  console.log(
    "\n" +
      dim(
        "Self-contained: no imports outside the standard library, and nothing to install.\n" +
          `\`policybook show ${policy.id}\` for when to use it, and when not to.`,
      ),
  );
  return 0;
}

// --- bench and render ---------------------------------------------------------

/**
 * These delegate to the same scripts CI runs.
 *
 * They are spawned rather than imported because both are written as scripts
 * with a `main()` that exits, and duplicating their logic here to avoid one
 * process is exactly the kind of drift the shared catalog module exists to
 * prevent.
 */
function delegate(script: string, args: ParsedArgs): number {
  const repoRoot = requireRepo();
  const scriptPath = join(repoRoot, "scripts", script);
  if (!existsSync(scriptPath)) {
    throw new Error(
      `${script} is only available inside a policybook checkout — it regenerates files in the repository.`,
    );
  }

  // On win32 pnpm is pnpm.cmd, which spawnSync only finds through a shell —
  // and a shell re-parses the arguments, so anything with a space (a checkout
  // under "C:\Users\John Doe\...") must be quoted by hand.
  const onWindows = process.platform === "win32";
  const argv = ["exec", "tsx", scriptPath, ...args.positionals];
  const run = spawnSync(
    "pnpm",
    onWindows ? argv.map((arg) => (/\s/.test(arg) ? `"${arg}"` : arg)) : argv,
    { cwd: repoRoot, stdio: "inherit", shell: onWindows },
  );
  if (run.error !== undefined) {
    throw new Error(
      `could not run ${script}: ${run.error.message}\n` +
        "This command needs the repository's toolchain (pnpm install first).",
    );
  }
  return run.status ?? 1;
}

export function commandBench(args: ParsedArgs): number {
  return delegate("bench.ts", args);
}

export function commandRender(args: ParsedArgs): number {
  return delegate("render-tables.ts", args);
}
