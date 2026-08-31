/**
 * The catalog, read from `policies/**` at build time.
 *
 * Nothing on this site is a copy. Every page reads the same `policy.json`, the
 * same `README.md` and the same `bench.json` that the tests, the CLI and the
 * benchmark pipeline read (concept.md §13.6), so a page cannot disagree with
 * the repository — the failure mode where documentation drifts from code is
 * removed rather than policed.
 *
 * This runs during `astro build`, in Node, and never reaches a browser.
 */

import { readFileSync, readdirSync, statSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
/** apps/web/src/lib -> the repository root. */
const REPO_ROOT = join(HERE, "..", "..", "..", "..");
const POLICIES_ROOT = join(REPO_ROOT, "policies");

export interface PolicyParam {
  name: string;
  type: string;
  default?: unknown;
  description: string;
}

export interface PolicySource {
  title?: string;
  authors?: string[];
  venue?: string | null;
  year?: number;
  url?: string | null;
  type?: string;
}

export interface PolicyMeta {
  id: string;
  name: string;
  domain: string;
  summary: string;
  source?: PolicySource;
  complexity: { time: string; space: string };
  params: PolicyParam[];
  tags: string[];
  recommended: boolean;
  ports: string[];
  notes?: string | null;
  status: string;
}

export interface BenchTrace {
  metrics: Record<string, number>;
  perf: { opsPerSec: number };
}

export interface BenchFile {
  policy: string;
  generatedAt: string;
  coreVersion: string;
  traces: Record<string, BenchTrace>;
}

export interface Policy {
  /** `cache/sieve` */
  id: string;
  /** `sieve` — the directory name, and the URL segment. */
  slug: string;
  domain: string;
  meta: PolicyMeta;
  /** The README exactly as committed, for the page to render. */
  readme: string;
  /** Null until the domain's bench item has run. */
  bench: BenchFile | null;
  /** The vectors, for the page's viewer. Null if unreadable. */
  vectors: unknown | null;
}

export interface Domain {
  /** `kv-cache` */
  id: string;
  /** The domain README, or null if it has not been written yet. */
  readme: string | null;
  policies: Policy[];
}

function readJson<T>(path: string): T | null {
  try {
    return JSON.parse(readFileSync(path, "utf8")) as T;
  } catch {
    return null;
  }
}

function readText(path: string): string | null {
  try {
    return readFileSync(path, "utf8");
  } catch {
    return null;
  }
}

function directoriesIn(root: string): string[] {
  try {
    return readdirSync(root)
      .filter((entry) => statSync(join(root, entry)).isDirectory())
      .sort();
  } catch {
    return [];
  }
}

/**
 * Every domain, with its policies, in a stable order.
 *
 * Domains come out in the order they were built rather than alphabetically:
 * cache first because it is where the ideas are simplest, kv-cache last
 * because it is the one people arrive for. A domain not named here still
 * appears, after the named ones, so adding one cannot silently hide it.
 */
const DOMAIN_ORDER = ["cache", "rate-limiter", "retry", "kv-cache"];

let cached: Domain[] | null = null;

export function loadCatalog(): Domain[] {
  if (cached !== null) return cached;

  const found = directoriesIn(POLICIES_ROOT);
  const ordered = [
    ...DOMAIN_ORDER.filter((id) => found.includes(id)),
    ...found.filter((id) => !DOMAIN_ORDER.includes(id)),
  ];

  cached = ordered.map((domainId) => {
    const domainDir = join(POLICIES_ROOT, domainId);

    const policies = directoriesIn(domainDir)
      .map((slug): Policy | null => {
        const dir = join(domainDir, slug);
        const meta = readJson<PolicyMeta>(join(dir, "policy.json"));
        if (meta === null) return null; // not a policy directory

        return {
          id: meta.id,
          slug,
          domain: domainId,
          meta,
          readme: readText(join(dir, "README.md")) ?? "",
          bench: readJson<BenchFile>(join(dir, "bench.json")),
          vectors: readJson<unknown>(join(dir, "vectors.json")),
        };
      })
      .filter((policy): policy is Policy => policy !== null);

    return {
      id: domainId,
      readme: readText(join(domainDir, "README.md")),
      policies,
    };
  });

  return cached;
}

/** Every policy across every domain, flattened. */
export function allPolicies(): Policy[] {
  return loadCatalog().flatMap((domain) => domain.policies);
}

/** One domain by id, or undefined. */
export function findDomain(id: string): Domain | undefined {
  return loadCatalog().find((domain) => domain.id === id);
}

/**
 * A domain's headline: the first paragraph of its README.
 *
 * Taken from the README rather than stored separately, so there is one place
 * to change it and no chance of the card and the page disagreeing.
 */
export function domainBlurb(domain: Domain): string {
  if (domain.readme === null) return "";
  const body = domain.readme.replace(/^#[^\n]*\n+/, "");
  const paragraph = body.split(/\n\s*\n/)[0] ?? "";
  return paragraph.replace(/\s+/g, " ").trim();
}

/**
 * The metric a domain's tables rank on, and the trace they rank it over.
 *
 * Mirrors `scripts/render-tables.ts`. It is duplicated rather than imported
 * because that script is a CLI module with its own side effects; the domain
 * test asserts the two agree.
 */
export const DOMAIN_HEADLINE: Record<string, { metric: string; label: string; trace: string }> = {
  cache: { metric: "hitRate", label: "Hit rate", trace: "zipf-1.0-100k" },
  "rate-limiter": { metric: "acceptRate", label: "Accept rate", trace: "overload" },
  retry: { metric: "successRate", label: "Success rate", trace: "outage-30s" },
  "kv-cache": {
    metric: "retainedAttentionMass",
    label: "Retained mass",
    trace: "decode-4096@512",
  },
};

/** A policy's headline number, or null if the domain has not been benched. */
export function headlineMetric(policy: Policy): number | null {
  const spec = DOMAIN_HEADLINE[policy.domain];
  if (spec === undefined || policy.bench === null) return null;
  return policy.bench.traces[spec.trace]?.metrics[spec.metric] ?? null;
}


/**
 * The policies a given one is most worth being compared against.
 *
 * Its neighbours in the domain's own benchmark ranking, rather than a
 * hand-written pairing. The interesting comparison is with whatever scores
 * closest — that is where a reader has an actual decision to make — and a
 * hardcoded list would go stale the moment a number moved, silently, with
 * nothing to catch it.
 *
 * Offline bounds are excluded: OPT is the ceiling, not a candidate. A policy at
 * either end of the ranking takes the neighbours on the side it has, so the
 * result is always `count` policies where the domain holds enough of them.
 */
export function neighboursOf(policy: Policy, count = 2): string[] {
  const domain = findDomain(policy.domain);
  if (domain === undefined) return [];

  const ranked = domain.policies
    .filter((entry) => entry.meta.status !== "offline-bound")
    .map((entry) => ({ slug: entry.slug, score: headlineMetric(entry) }))
    .filter((entry): entry is { slug: string; score: number } => entry.score !== null)
    .sort((a, b) => b.score - a.score || a.slug.localeCompare(b.slug))
    .map((entry) => entry.slug);

  const at = ranked.indexOf(policy.slug);
  if (at === -1) return [];

  const window = count + 1;
  const start = Math.max(0, Math.min(at - Math.floor(count / 2), ranked.length - window));
  return ranked.slice(start, start + window).filter((slug) => slug !== policy.slug);
}
