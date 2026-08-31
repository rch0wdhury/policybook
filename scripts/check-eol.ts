/**
 * Fails if any tracked text file contains a carriage return.
 *
 * The repository is edited from Windows and verified from WSL, so a stray CRLF
 * is a real hazard: a CRLF shell script will not run under bash, and a CRLF
 * `vectors.json` hashes differently from the same file on Linux. `.gitattributes`
 * normalises on commit, but this checks the working tree, which is what every
 * tool actually reads.
 *
 * Usage: pnpm eol
 */

import { execFileSync } from "node:child_process";
import { readFileSync } from "node:fs";

const BINARY_EXTENSIONS = new Set([
  ".png",
  ".jpg",
  ".jpeg",
  ".gif",
  ".webp",
  ".ico",
  ".pdf",
  ".woff",
  ".woff2",
  ".ttf",
  ".otf",
  ".zip",
  ".gz",
  ".xz",
  ".tar",
  ".wasm",
  ".whl",
]);

const CR = 0x0d;
const NUL = 0x00;

function trackedFiles(): string[] {
  const raw = execFileSync("git", ["ls-files", "-z"], { encoding: "buffer" });
  return raw
    .toString("utf8")
    .split("\0")
    .filter((path) => path.length > 0);
}

function extensionOf(path: string): string {
  const dot = path.lastIndexOf(".");
  return dot === -1 ? "" : path.slice(dot).toLowerCase();
}

function main(): void {
  const offenders: string[] = [];
  let scanned = 0;

  for (const path of trackedFiles()) {
    if (BINARY_EXTENSIONS.has(extensionOf(path))) continue;

    let contents: Buffer;
    try {
      contents = readFileSync(path);
    } catch {
      // Deleted from the working tree but still in the index; nothing to check.
      continue;
    }

    // A NUL byte means the file is binary regardless of its extension.
    if (contents.includes(NUL)) continue;

    scanned += 1;
    if (contents.includes(CR)) offenders.push(path);
  }

  if (offenders.length > 0) {
    console.error(`CRLF found in ${offenders.length} of ${scanned} text files:\n`);
    for (const path of offenders) console.error(`  ${path}`);
    console.error(
      "\nEvery text file in this repository is LF only (see .gitattributes).\n" +
        "Fix with:  sed -i 's/\\r$//' <file>\n",
    );
    process.exit(1);
  }

  console.log(`eol: ${scanned} text files checked, all LF.`);
}

main();
