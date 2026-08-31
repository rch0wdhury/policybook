/**
 * Copy the registry into the package before `npm pack`/`npm publish`.
 *
 * The CLI's commands read policy sources, vectors and shared helpers from the
 * repository tree — `add` inlines helpers from packages/, `verify --lang c`
 * configures packages/c with CMake. An npm install has none of that unless
 * the tarball carries it, so this runs as `prepack` and mirrors the pieces
 * next to dist/, where `requireRepo` looks for them. The copies are
 * gitignored; a checkout never reads them because the working-directory
 * walk-up wins there.
 */

import { cpSync, rmSync } from "node:fs";
import { dirname, join, sep } from "node:path";
import { fileURLToPath } from "node:url";

const packageRoot = dirname(dirname(fileURLToPath(import.meta.url)));
const repoRoot = dirname(dirname(packageRoot));

const SKIP = new Set(["__pycache__", "node_modules", ".pytest_cache", ".mypy_cache"]);

function keep(source) {
  return !source.split(sep).some((part) => SKIP.has(part) || part.endsWith(".pyc"));
}

const TREES = [
  "policies",
  join("packages", "core", "src"),
  join("packages", "python", "policybook"),
  join("packages", "c"),
];

rmSync(join(packageRoot, "policies"), { recursive: true, force: true });
rmSync(join(packageRoot, "packages"), { recursive: true, force: true });

for (const tree of TREES) {
  cpSync(join(repoRoot, tree), join(packageRoot, tree), {
    recursive: true,
    filter: keep,
  });
}

console.log("prepack: registry copied into the package.");
