/**
 * Enforces the site's JavaScript budgets against the built output.
 *
 * A budget that is not measured is a wish. These are read from `dist/` after a
 * real build, gzipped the way a server would serve them, and the build fails
 * when one is exceeded.
 *
 * Gzipped rather than raw, because gzipped is what crosses the network and raw
 * bytes would flatter every change that adds repetitive code.
 *
 *   pnpm tsx apps/web/scripts/check-bundle-size.ts --viz cache --max-gz 20480
 *   pnpm tsx apps/web/scripts/check-bundle-size.ts --all
 */

import { gzipSync } from "node:zlib";
import { readFileSync, readdirSync, statSync } from "node:fs";
import { dirname, join, relative } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const DIST = join(HERE, "..", "dist");

/**
 * The standing budgets, checked by `--all`.
 *
 * `match` names the chunks a budget covers, and the budget applies to their
 * combined gzipped size — a visualisation split across two chunks has not
 * become cheaper for being split. The shared canvas helpers and repaint wiring
 * appear under every viz because loading any one of them pays for that code;
 * Rollup may emit the two shared modules as one chunk (named for either) or
 * two, so both names stay on every viz budget.
 *
 * Matching is on the chunk's module name, not a substring of its path. That is
 * not fussiness: `CacheViz` is a substring of `KvCacheViz`, so a substring
 * matcher quietly charged the cache budget for the kv-cache renderer and would
 * have reported an overrun against whichever budget happened to be checked
 * first.
 */
const BUDGETS: { name: string; match: string[]; maxGz: number }[] = [
  // Per: each visualisation under 20 KB gzipped.
  { name: "cache viz", match: ["CacheViz", "canvas", "useRepaint"], maxGz: 20 * 1024 },
  { name: "rate-limiter viz", match: ["RateLimiterViz", "canvas", "useRepaint"], maxGz: 20 * 1024 },
  { name: "kv-cache viz", match: ["KvCacheViz", "canvas", "useRepaint"], maxGz: 20 * 1024 },
  // The core and the policies, which the worker pulls in. Generous, because it
  // carries real algorithm implementations rather than framework.
  { name: "simulation worker", match: ["sim-worker"], maxGz: 200 * 1024 },
];

/**
 * Does a built chunk come from the module called `needle`?
 *
 * `dist/_astro/CacheViz.CBMJFV5c.js` is `CacheViz`, and `sim-worker-DGKO2DNp.js`
 * is `sim-worker`: Vite writes the hash after a dot or after a dash, and its
 * *length* is a build setting, not a constant — a parser that assumed eight
 * characters turned a hash-length change into "no matching chunk", which the
 * old zero-match branch then waved through. So the name must match exactly up
 * to the separator, and whatever follows must look like one hash segment.
 *
 * Equality on the name, not substring: `CacheViz` is a substring of
 * `KvCacheViz`, and a substring matcher quietly charged the cache budget for
 * the kv-cache renderer.
 */
function matchesModule(path: string, needle: string): boolean {
  const base = path.split("/").pop() ?? path;
  if (base === `${needle}.js`) return true;
  if (!base.startsWith(needle) || !base.endsWith(".js")) return false;
  const rest = base.slice(needle.length, -".js".length);
  return /^[.-][A-Za-z0-9_-]+$/.test(rest);
}

/**
 * Every byte of JavaScript the site ships, gzipped.
 */
const TOTAL_MAX_GZ = 200 * 1024;

interface Chunk {
  path: string;
  gz: number;
}

function chunks(): Chunk[] {
  const found: Chunk[] = [];
  const walk = (dir: string): void => {
    for (const entry of readdirSync(dir)) {
      const path = join(dir, entry);
      if (statSync(path).isDirectory()) walk(path);
      else if (entry.endsWith(".js")) {
        found.push({
          path: relative(DIST, path).split("\\").join("/"),
          gz: gzipSync(readFileSync(path)).length,
        });
      }
    }
  };
  walk(DIST);
  return found;
}

function kb(bytes: number): string {
  return `${(bytes / 1024).toFixed(1)} KB`;
}

function check(name: string, match: string[], maxGz: number, all: Chunk[]): boolean {
  const matched = all.filter((chunk) =>
    match.some((needle) => matchesModule(chunk.path, needle)),
  );

  if (matched.length === 0) {
    // Fail closed. A budget measuring nothing is a guard that has stopped
    // guarding: rename the chunk and every ceiling on it passes vacuously
    // forever. A rename must show up here, as a red build naming the budget,
    // not as a quietly green one.
    console.log(`  FAIL ${name}: no chunk matches [${match.join(", ")}] — renamed or removed?`);
    return false;
  }

  const total = matched.reduce((sum, chunk) => sum + chunk.gz, 0);
  const ok = total <= maxGz;
  console.log(
    `  ${ok ? "ok  " : "FAIL"} ${name}: ${kb(total)} gzipped of ${kb(maxGz)} ` +
      `(${matched.length} chunk${matched.length === 1 ? "" : "s"})`,
  );
  if (!ok) {
    for (const chunk of matched.sort((a, b) => b.gz - a.gz)) {
      console.log(`         ${chunk.path} — ${kb(chunk.gz)}`);
    }
  }
  return ok;
}

function main(): void {
  const argv = process.argv.slice(2);
  const all = chunks();

  if (all.length === 0) {
    console.error("check-bundle-size: no JavaScript in dist. Build the site first.");
    process.exit(1);
  }

  let passed = true;

  if (argv.includes("--all")) {
    console.log("check-bundle-size: every budget");
    for (const budget of BUDGETS) {
      passed = check(budget.name, budget.match, budget.maxGz, all) && passed;
    }
  } else {
    const vizIndex = argv.indexOf("--viz");
    const maxIndex = argv.indexOf("--max-gz");
    if (vizIndex === -1 || maxIndex === -1) {
      console.error(
        "check-bundle-size: pass --all, or --viz <name> --max-gz <bytes>.",
      );
      process.exit(1);
    }

    const name = argv[vizIndex + 1]!;
    const maxGz = Number(argv[maxIndex + 1]);
    const budget = BUDGETS.find((entry) => entry.name.startsWith(name));

    console.log(`check-bundle-size: ${name}`);
    passed = check(
      budget?.name ?? name,
      budget?.match ?? [name],
      Number.isFinite(maxGz) ? maxGz : (budget?.maxGz ?? 20 * 1024),
      all,
    );
  }

  /**
   * The whole site, not just the named budgets.
   *
   * puts the core plus every policy under 200 KB gzipped. This
   * is that ceiling, and it is a loose one: the site ships around 50 KB because
   * policies load lazily, so the total has a lot of room before it complains.
   *
   * It is deliberately not the guard against stray files. A glob for the
   * tutorial's examples once also matched its *test* file and shipped 85 KB of
   * vitest; the total went to 138 KB and this check would still have passed,
   * which I confirmed by reintroducing the bug rather than assuming. That is
   * what `testArtifacts` below is for.
   */
  const total = all.reduce((sum, chunk) => sum + chunk.gz, 0);
  const totalOk = total <= TOTAL_MAX_GZ;
  console.log(
    `  ${totalOk ? "ok  " : "FAIL"} total shipped JavaScript: ${kb(total)} gzipped of ` +
      `${kb(TOTAL_MAX_GZ)} across ${all.length} chunks`,
  );

  if (!totalOk) {
    console.log("         largest chunks:");
    for (const chunk of [...all].sort((a, b) => b.gz - a.gz).slice(0, 8)) {
      console.log(`         ${chunk.path} — ${kb(chunk.gz)}`);
    }
  }

  /**
   * Nothing that is a test may be in the site.
   *
   * A size budget catches bloat; this catches the specific, silent thing that
   * caused it — a build artifact that has no business being served at all. It
   * is also the honest check, because 85 KB of test framework is wrong at any
   * size.
   */
  // Matched against the raw filename, not through the module matcher: name
  // matching treats everything past the first separator as hash, so in
  // `evict-newest.test-a1b2c3d4.js` the word this is looking for reads as part
  // of the hash and is gone before the check runs. Found by reintroducing the
  // bug and watching the guard pass.
  const strays = all.filter((chunk) =>
    /\.(test|spec)[.\-]/.test(chunk.path.split("/").pop() ?? ""),
  );
  if (strays.length > 0) {
    console.log(`  FAIL test files in the bundle: ${strays.length}`);
    for (const chunk of strays) {
      console.log(`         ${chunk.path} — ${kb(chunk.gz)}`);
    }
  }

  if (!passed || !totalOk || strays.length > 0) process.exit(1);
}

main();
