/**
 * Injects benchmark tables into the READMEs, between the bench markers.
 *
 * READMEs are the product (concept.md §2), and a README whose numbers were
 * typed by hand goes stale the first time a policy changes. Everything between
 * `<!-- bench:start -->` and `<!-- bench:end -->` is generated from the
 * committed `bench.json` files and must never be edited directly.
 *
 * Usage: pnpm render
 */

import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { discoverPolicies, findRepoRoot } from "../packages/vectors/src/discover";
import type { DiscoveredPolicy } from "../packages/vectors/src/types";

interface BenchTraceResult {
  metrics: Record<string, number>;
  perf: { opsPerSec: number };
}

interface BenchFile {
  policy: string;
  generatedAt: string;
  coreVersion: string;
  traces: Record<string, BenchTraceResult>;
}

const START = "<!-- bench:start -->";
const END = "<!-- bench:end -->";

/**
 * How each domain's tables are headed, ranked and captioned.
 *
 * The columns differ because the domains differ: a cache reports a hit rate and
 * an eviction count, a limiter an accept rate and the burst it let through, a
 * retry policy a success rate and the herd it caused. Guessing at a shared
 * shape would produce a table that fits none of them.
 */
interface DomainTableSpec {
  /** The metric the tables rank by, and its column heading. */
  metric: string;
  metricLabel: string;
  /** The trace ranking happens on. */
  primaryTrace: string;
  /** A second metric worth a column beside the first. */
  secondary: string;
  secondaryLabel: string;
  /** The sentence under a policy's own table. */
  caption: string;
}

const DOMAIN_TABLES: Record<string, DomainTableSpec> = {
  cache: {
    metric: "hitRate",
    metricLabel: "Hit rate",
    primaryTrace: "zipf-1.0-100k",
    secondary: "evictions",
    secondaryLabel: "Evictions",
    caption:
      "Hit rate is the number that matters, and the bracketed figure is the gap to the best online policy in this domain on that trace. Throughput is machine-dependent and is never asserted.",
  },
  "rate-limiter": {
    metric: "acceptRate",
    metricLabel: "Accept rate",
    primaryTrace: "overload",
    secondary: "maxBurst100ms",
    secondaryLabel: "Peak / 100 ms",
    caption:
      "Accept rate alone ranks nothing here: under sustained overload every correct limiter admits rate times time, so these converge by construction. Read the domain README, because the choice is made on memory, on behaviour at a window seam, and on what distributes. Throughput is machine-dependent and is never asserted.",
  },
  retry: {
    metric: "successRate",
    metricLabel: "Success rate",
    primaryTrace: "outage-30s",
    secondary: "peakRetryShare",
    secondaryLabel: "Peak retry share",
    caption:
      "Success rate is what a client gets. Peak retry share is what the recovering service pays, and the two trade against each other. Throughput is machine-dependent and is never asserted.",
  },
  "kv-cache": {
    metric: "retainedAttentionMass",
    metricLabel: "Retained mass",
    primaryTrace: "decode-4096@512",
    secondary: "heavyHitterRecall",
    secondaryLabel: "Heavy-hitter recall",
    caption:
      "Both columns are proxies for output quality, not measurements of it, so read the domain README before drawing conclusions. They also disagree: the policies leading on retained mass are not the ones leading on heavy-hitter recall, and ranking by either alone will mislead you. Rows are one budget each, because the ordering changes with the budget. Throughput is machine-dependent and is never asserted.",
  },
};

const DEFAULT_TABLE: DomainTableSpec = DOMAIN_TABLES["cache"]!;

function replaceBetweenMarkers(markdown: string, body: string, path: string): string {
  const start = markdown.indexOf(START);
  const end = markdown.indexOf(END);
  if (start === -1 || end === -1 || end < start) {
    throw new Error(`${path}: missing or malformed ${START} / ${END} markers`);
  }
  return markdown.slice(0, start + START.length) + "\n" + body + "\n" + markdown.slice(end);
}

function formatNumber(value: number | undefined): string {
  if (value === undefined) return "-";
  if (Number.isInteger(value)) return value.toLocaleString("en-US");
  return value.toFixed(4);
}

/** A policy's own table: every metric on every trace. */
function policyTable(policy: DiscoveredPolicy, bench: BenchFile, best: Map<string, number>): string {
  const traces = Object.keys(bench.traces);
  const spec = DOMAIN_TABLES[policy.domain] ?? DEFAULT_TABLE;
  const metric = spec.metric;
  const offline = policy.meta.status === "offline-bound";

  const lines: string[] = [];
  lines.push(`| Trace | ${spec.metricLabel} | ${spec.secondaryLabel} | Throughput |`);
  lines.push(`|---|---:|---:|---:|`);

  for (const trace of traces) {
    const result = bench.traces[trace]!;
    const value = result.metrics[metric];
    const leader = best.get(trace);
    // Show the gap to the best online policy, which is what a reader is
    // actually comparing against.
    const gap =
      !offline && value !== undefined && leader !== undefined && leader > value
        ? ` (−${(leader - value).toFixed(4)})`
        : "";
    lines.push(
      `| \`${trace}\` | ${formatNumber(value)}${gap} | ${formatNumber(result.metrics[spec.secondary])} | ${formatNumber(result.perf.opsPerSec)}/s |`,
    );
  }

  lines.push("");
  lines.push(
    offline
      ? "Offline bound: this row is what no online policy can exceed, not a result to compare against."
      : spec.caption,
  );
  lines.push("");
  lines.push(
    `<sub>Generated by \`pnpm bench && pnpm render\` from core ${bench.coreVersion}. Do not edit.</sub>`,
  );
  return lines.join("\n");
}

/** The domain's table: every policy against every trace. */
/**
 * The root README's index and its per-domain tables.
 *
 * Generated for the same reason every other table is: a hand-written count of
 * how many policies exist is wrong the first time someone adds one, and a
 * README that miscounts its own contents is the least trustworthy thing a
 * repository can ship. The domain descriptions come from each domain README's
 * first sentence, so there is one place they are written.
 *
 * The link rewriting matters. `domainTable` emits links relative to a domain
 * directory (`sieve/`), which are correct in `policies/cache/README.md` and
 * broken at the root, so they are rewritten to full repository paths here.
 */
function rootSection(
  repoRoot: string,
  byDomain: Map<string, { policy: DiscoveredPolicy; bench: BenchFile }[]>,
): string {
  const lines: string[] = [];

  lines.push("| Domain | What it decides | Policies | Ranked by |");
  lines.push("|---|---|---:|---|");

  for (const [domain, entries] of byDomain) {
    const spec = DOMAIN_TABLES[domain] ?? DEFAULT_TABLE;
    lines.push(
      `| [${domain}](policies/${domain}/) | ${domainSentence(repoRoot, domain)} | ` +
        `${entries.length} | ${spec.metricLabel} on \`${spec.primaryTrace}\` |`,
    );
  }

  for (const [domain, entries] of byDomain) {
    lines.push("");
    lines.push(`### ${domain}`);
    lines.push("");
    lines.push(
      domainTable(domain, entries).replace(/\]\(([^)/]+)\/\)/g, `](policies/${domain}/$1/)`),
    );
  }

  return lines.join("\n");
}

/**
 * One line describing a domain, taken from its README rather than written here.
 *
 * The first sentence of the first real paragraph. If a domain README has no
 * prose the cell is left empty rather than invented.
 */
function domainSentence(repoRoot: string, domain: string): string {
  const path = join(repoRoot, "policies", domain, "README.md");
  if (!existsSync(path)) return "";

  const body = readFileSync(path, "utf8").replace(/^#[^\n]*\n/, "");
  const paragraph = body
    .split(/\n\s*\n/)
    .map((block) => block.trim())
    .find((block) => block !== "" && !block.startsWith("#") && !block.startsWith("<!--"));
  if (paragraph === undefined) return "";

  const flat = paragraph.replace(/\s+/g, " ");
  const sentence = /^(.+?[.!?])(\s|$)/.exec(flat);
  return (sentence?.[1] ?? flat).trim();
}

function domainTable(domain: string, entries: { policy: DiscoveredPolicy; bench: BenchFile }[]): string {
  const spec = DOMAIN_TABLES[domain] ?? DEFAULT_TABLE;
  const metric = spec.metric;
  const primary = spec.primaryTrace;
  const traces = Object.keys(entries[0]?.bench.traces ?? {});

  const ranked = [...entries].sort((left, right) => {
    // Any offline bound sits at the end, as a reference rather than a rival.
    const leftOffline = left.policy.meta.status === "offline-bound";
    const rightOffline = right.policy.meta.status === "offline-bound";
    if (leftOffline !== rightOffline) return leftOffline ? 1 : -1;
    const leftValue = left.bench.traces[primary]?.metrics[metric] ?? 0;
    const rightValue = right.bench.traces[primary]?.metrics[metric] ?? 0;
    return rightValue - leftValue;
  });

  const lines: string[] = [];
  lines.push(`${spec.metricLabel} on each canonical trace, best first on \`${primary}\`.`);
  lines.push("");
  lines.push(`| Policy | ${traces.map((trace) => `\`${trace}\``).join(" | ")} |`);
  lines.push(`|---|${traces.map(() => "---:").join("|")}|`);

  for (const { policy, bench } of ranked) {
    const label =
      policy.meta.status === "offline-bound"
        ? `**[${policy.meta.name}](${policy.name}/)** *(offline bound)*`
        : `[${policy.meta.name}](${policy.name}/)`;
    const cells = traces.map((trace) => formatNumber(bench.traces[trace]?.metrics[metric]));
    lines.push(`| ${label} | ${cells.join(" | ")} |`);
  }

  lines.push("");
  lines.push(
    `<sub>Generated by \`pnpm bench && pnpm render\` from core ${entries[0]?.bench.coreVersion ?? ""}. Do not edit.</sub>`,
  );
  return lines.join("\n");
}

function main(): void {
  const repoRoot = findRepoRoot();
  const policies = discoverPolicies(repoRoot);

  const byDomain = new Map<string, { policy: DiscoveredPolicy; bench: BenchFile }[]>();
  let rendered = 0;

  for (const policy of policies) {
    const path = join(policy.dir, "bench.json");
    if (!existsSync(path)) continue;
    const bench = JSON.parse(readFileSync(path, "utf8")) as BenchFile;
    const entries = byDomain.get(policy.domain) ?? [];
    entries.push({ policy, bench });
    byDomain.set(policy.domain, entries);
  }

  for (const [domain, entries] of byDomain) {
    const metric = (DOMAIN_TABLES[domain] ?? DEFAULT_TABLE).metric;

    // The best online result per trace, used for the gap column.
    const best = new Map<string, number>();
    for (const { policy, bench } of entries) {
      if (policy.meta.status === "offline-bound") continue;
      for (const [trace, result] of Object.entries(bench.traces)) {
        const value = result.metrics[metric];
        if (value === undefined) continue;
        if (!best.has(trace) || value > best.get(trace)!) best.set(trace, value);
      }
    }

    for (const { policy, bench } of entries) {
      const readmePath = join(policy.dir, "README.md");
      const markdown = readFileSync(readmePath, "utf8");
      const updated = replaceBetweenMarkers(markdown, policyTable(policy, bench, best), readmePath);
      if (updated !== markdown) writeFileSync(readmePath, updated);
      rendered += 1;
    }

    const domainReadme = join(repoRoot, "policies", domain, "README.md");
    if (existsSync(domainReadme)) {
      const markdown = readFileSync(domainReadme, "utf8");
      const updated = replaceBetweenMarkers(markdown, domainTable(domain, entries), domainReadme);
      if (updated !== markdown) writeFileSync(domainReadme, updated);
      rendered += 1;
    }
  }

  // The root README last, because it summarises what the loop above wrote.
  const rootReadme = join(repoRoot, "README.md");
  if (existsSync(rootReadme)) {
    const markdown = readFileSync(rootReadme, "utf8");
    const updated = replaceBetweenMarkers(markdown, rootSection(repoRoot, byDomain), rootReadme);
    if (updated !== markdown) writeFileSync(rootReadme, updated);
    rendered += 1;
  }

  console.log(`render: updated ${rendered} README section(s).`);
}

main();
