/**
 * No em-dashes and no semicolons in any rendered prose.
 *
 * A style rule is only real if something fails when it is broken, and this one
 * spans too many sources to police by eye: page templates, island strings,
 * canvas captions, README prose, policy.json summaries. So the check reads the
 * built pages, drops everything that is code (script, style, pre, code spans),
 * strips the tags, and counts what a reader would actually see. Code keeps its
 * semicolons because that is syntax, not style.
 *
 * Usage: node apps/web/scripts/check-prose.mjs
 */

import { readdirSync, readFileSync, statSync } from "node:fs";
import { join, relative } from "node:path";
import { fileURLToPath } from "node:url";

const dist = fileURLToPath(new URL("../dist", import.meta.url));

function pages(dir) {
  const out = [];
  for (const entry of readdirSync(dir)) {
    const path = join(dir, entry);
    if (statSync(path).isDirectory()) out.push(...pages(path));
    else if (entry.endsWith(".html")) out.push(path);
  }
  return out;
}

/** The text a reader sees: no code, no markup, entities resolved. */
function visibleText(html) {
  return html
    .replace(/<script[\s\S]*?<\/script>/gi, " ")
    .replace(/<style[\s\S]*?<\/style>/gi, " ")
    .replace(/<pre[\s\S]*?<\/pre>/gi, " ")
    .replace(/<code[\s\S]*?<\/code>/gi, " ")
    .replace(/<!--[\s\S]*?-->/g, " ")
    .replace(/<[^>]+>/g, " ")
    .replace(/&#(\d+);/g, (_, n) => String.fromCodePoint(Number(n)))
    .replace(/&#x([0-9a-f]+);/gi, (_, n) => String.fromCodePoint(parseInt(n, 16)))
    .replace(/&amp;/g, "&")
    .replace(/&lt;/g, "<")
    .replace(/&gt;/g, ">")
    .replace(/&quot;/g, '"')
    .replace(/&#39;/g, "'")
    .replace(/&nbsp;/g, " ");
}

const all = pages(dist);
if (all.length === 0) {
  console.error("check-prose: no pages found in dist — build the site first.");
  process.exit(1);
}

let bad = 0;
for (const page of all) {
  const text = visibleText(readFileSync(page, "utf8"));
  const findings = [];
  const dashes = (text.match(/—/g) ?? []).length;
  if (dashes > 0) findings.push(`${dashes} em-dash(es)`);
  const semis = (text.match(/;/g) ?? []).length;
  if (semis > 0) findings.push(`${semis} semicolon(s)`);
  if (findings.length > 0) {
    bad += 1;
    const context = text.match(/[^.\n]{0,60}[—;][^.\n]{0,60}/g)?.slice(0, 3) ?? [];
    console.error(`FAIL ${relative(dist, page)}: ${findings.join(", ")}`);
    for (const snippet of context) console.error(`     …${snippet.trim()}…`);
  }
}

if (bad > 0) {
  console.error(`\ncheck-prose: ${bad} page(s) carry em-dashes or semicolons in visible prose.`);
  process.exit(1);
}
console.log(`check-prose: ${all.length} page(s), no em-dashes or semicolons in visible prose.`);
