/**
 * Asserts things about the built site, by reading `dist/`.
 *
 * The site is static, so the output is the whole truth: if a page does not
 * contain what it should, the file says so. This is deliberately a text
 * assertion rather than a DOM one — it needs no browser, runs in milliseconds,
 * and cannot pass because a headless browser rendered something a reader would
 * never see.
 *
 * Every web item from T38 onward uses it, which is why the arguments repeat:
 *
 *   node apps/web/scripts/check-dist.mjs \
 *     --page index.html --contains policybook \
 *     --page d/cache/index.html --contains SIEVE
 *
 * `--contains` applies to the `--page` before it, so several assertions can
 * share one page and several pages can each carry their own.
 *
 * `--missing` is the opposite, for asserting something is *not* there — a
 * placeholder that should have been replaced, for instance.
 */

import { existsSync, readFileSync, readdirSync, statSync } from "node:fs";
import { dirname, join, relative } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const DIST = join(HERE, "..", "dist");

function parseArgs(argv) {
  const pages = [];
  let current = null;

  for (let i = 0; i < argv.length; i += 1) {
    const flag = argv[i];
    const value = argv[i + 1];

    if (flag === "--page") {
      current = { page: value, contains: [], missing: [] };
      pages.push(current);
      i += 1;
    } else if (flag === "--contains" || flag === "--missing") {
      if (current === null) {
        throw new Error(`${flag} came before any --page`);
      }
      (flag === "--contains" ? current.contains : current.missing).push(value);
      i += 1;
    } else {
      throw new Error(`unknown argument: ${flag}`);
    }
  }

  return pages;
}

/** Every HTML file in dist, for the error message when a page is missing. */
function builtPages() {
  const found = [];
  const walk = (dir) => {
    for (const entry of readdirSync(dir)) {
      const path = join(dir, entry);
      if (statSync(path).isDirectory()) walk(path);
      else if (entry.endsWith(".html")) found.push(relative(DIST, path).split("\\").join("/"));
    }
  };
  if (existsSync(DIST)) walk(DIST);
  return found.sort();
}

function main() {
  const pages = parseArgs(process.argv.slice(2));
  if (pages.length === 0) {
    console.error("check-dist: nothing to check. Pass --page <path> --contains <text>.");
    process.exit(1);
  }

  if (!existsSync(DIST)) {
    console.error("check-dist: apps/web/dist does not exist. Build the site first.");
    process.exit(1);
  }

  const failures = [];
  let assertions = 0;

  for (const { page, contains, missing } of pages) {
    const path = join(DIST, page);

    if (!existsSync(path)) {
      const available = builtPages();
      failures.push(
        `${page} was not built. ${available.length} page(s) exist:\n` +
          available.map((name) => `      ${name}`).join("\n"),
      );
      continue;
    }

    const html = readFileSync(path, "utf8");

    for (const needle of contains) {
      assertions += 1;
      if (!html.includes(needle)) {
        failures.push(`${page} does not contain ${JSON.stringify(needle)}`);
      }
    }

    for (const needle of missing) {
      assertions += 1;
      if (html.includes(needle)) {
        failures.push(`${page} still contains ${JSON.stringify(needle)}`);
      }
    }
  }

  if (failures.length > 0) {
    console.error("check-dist: the built site is not what it should be\n");
    for (const failure of failures) console.error(`  ${failure}`);
    process.exit(1);
  }

  console.log(
    `check-dist: ${assertions} assertion(s) over ${pages.length} page(s), all satisfied.`,
  );
}

main();
