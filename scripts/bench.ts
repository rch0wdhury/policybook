/**
 * Runs every policy over its domain's canonical traces and writes `bench.json`.
 *
 * The numbers in this registry are the point of it. A decision table that says
 * "SIEVE is scan-resistant" is worth much less than one that says SIEVE reaches
 * 0.73 where LRU reaches 0.61 on a named, reproducible trace — so the tables are
 * generated from measurements rather than written by hand (concept.md §8.4).
 *
 * Each policy's results are committed. `metrics` are deterministic and are
 * asserted; `perf` is machine-dependent and is only ever informational
 *.
 *
 * Usage:
 *   pnpm bench cache                              # one domain
 *   pnpm bench --all                              # every domain
 *   pnpm bench cache -- --frozen-time <iso>       # stable generatedAt, for tests
 */

import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { CORE_VERSION } from "../packages/core/src/index";
import { stableJson } from "../packages/core/src/json";
import {
  CACHE_TRACES,
  cacheMetrics,
  generateCacheTrace,
  runCacheTrace,
} from "../packages/core/src/domains/cache";
import {
  RATE_LIMITER_TRACES,
  generateRateLimiterTrace,
  rateLimiterMetrics,
  runRateLimiterTrace,
} from "../packages/core/src/domains/rate-limiter";
import {
  RETRY_TRACES,
  retryMetrics,
  runRetryEpisodes,
} from "../packages/core/src/domains/retry";
import type { RetryPolicy } from "../packages/core/src/domains/retry";
import {
  KV_CACHE_BUDGETS,
  KV_CACHE_TRACES,
  kvCacheMetrics,
  runKvCacheTrace,
} from "../packages/core/src/domains/kv-cache";
import { discoverPolicies, findRepoRoot, loadFactory } from "../packages/vectors/src/discover";
import type { DiscoveredPolicy, JsonValue } from "../packages/vectors/src/types";
import type { PolicyFactory } from "../packages/vectors/src/run";

/** One trace's results for one policy. */
export interface BenchTraceResult {
  metrics: Record<string, number>;
  perf: { opsPerSec: number };
}

/** What a domain needs to provide so its policies can be benchmarked. */
interface DomainBench {
  /** Canonical trace ids, in the order they should appear in tables. */
  traces: string[];
  /** The metric tables rank by, and the trace they rank on. */
  primaryMetric: string;
  primaryTrace: string;
  /** Run one policy over one trace. */
  run(factory: PolicyFactory, policy: DiscoveredPolicy, traceId: string): BenchTraceResult;
}

/** How many times to repeat a measurement before taking the best throughput. */
const PERF_REPEATS = 3;

/**
 * How long one measurement must run before its timing means anything.
 *
 * A trace is not always big enough to time. The rate-limiter's `bursty` is
 * 3,020 events, which at ten million a second is a third of a millisecond —
 * shorter than the noise in `performance.now()` and far shorter than the JIT
 * takes to settle. Measured that way, throughput swung by more than the 25%
 * damping below and `bench.json` churned on every single run, which is exactly
 * what that damping exists to prevent.
 *
 * So a measurement repeats the whole run until it has taken at least this long,
 * and divides the total events by the total time. Fifty milliseconds is enough
 * for the figure to be stable to a few percent without making a bench run slow.
 */
const PERF_MIN_MS = 50;

/**
 * Time `run` properly and report events per second.
 *
 * `run` is expected to be repeatable — the harnesses build a fresh policy each
 * call — and `events` is how many events one call processes.
 */
function measureThroughput(events: number, run: () => void): number {
  let fastest = 0;

  for (let repeat = 0; repeat < PERF_REPEATS; repeat += 1) {
    const started = performance.now();
    let iterations = 0;
    let elapsed = 0;

    do {
      run();
      iterations += 1;
      elapsed = performance.now() - started;
    } while (elapsed < PERF_MIN_MS);

    const opsPerSec = elapsed > 0 ? (events * iterations) / (elapsed / 1000) : 0;
    if (opsPerSec > fastest) fastest = opsPerSec;
  }

  return roundThroughput(fastest);
}

/**
 * Throughput movement below this is treated as measurement noise and the
 * recorded figure is left alone, so re-running produces no diff. A real
 * regression is the perf guard's job, and it has its own tighter thresholds.
 */
const PERF_NOISE = 0.25;

/**
 * Round throughput to three significant figures.
 *
 * Two reasons, and both matter. Throughput is machine-dependent, so a figure
 * like 25,189,805/s claims a precision it does not have — three figures is
 * already generous. And `bench.json` is committed, so recording the raw
 * measurement would make every re-run produce a diff from timing noise alone.
 */
function roundThroughput(opsPerSec: number): number {
  if (opsPerSec <= 0) return 0;
  const magnitude = Math.pow(10, Math.floor(Math.log10(opsPerSec)) - 2);
  return Math.round(opsPerSec / magnitude) * magnitude;
}

/**
 * The reference configuration, spelled in each rate-limiter policy's own terms.
 *
 * Every policy is benchmarked at the domain's reference budget — 100 permits a
 * second, burst 100 — but they name their limits differently, so the mapping has
 * to be written out. Benching a policy at its own defaults instead would compare
 * policies configured differently, which is worse than no table at all.
 *
 * A policy with no entry here is a hard error rather than a silent fallback, so
 * a new policy cannot join the table on the wrong settings.
 */
const RATE_LIMITER_BENCH_PARAMS: Record<string, Record<string, JsonValue>> = {
  "rate-limiter/fixed-window": { limit: 100, windowMs: 1_000 },
  "rate-limiter/sliding-log": { limit: 100, windowMs: 1_000 },
  "rate-limiter/sliding-counter": { limit: 100, windowMs: 1_000 },
  "rate-limiter/token-bucket": { ratePerSec: 100, burst: 100 },
  // The reference burst maps onto `capacity`, which makes this identical to the
  // token bucket — the two are one algorithm, and the identical row is the
  // honest result rather than a bug. Its own default of 1 is the smoothing
  // configuration, which is a different question from "what does the reference
  // budget cost?".
  "rate-limiter/leaky-bucket": { ratePerSec: 100, capacity: 100 },
  "rate-limiter/gcra": { ratePerSec: 100, burst: 100 },
  // Per-minute ceilings: 100 a second is 6,000 a minute. Every canonical
  // arrival costs one unit, so both dimensions are set to the same figure and
  // the policy is limited by whichever binds first — in practice both at once.
  "rate-limiter/dual-bucket": { requestsPerMin: 6_000, tokensPerMin: 6_000 },
};

/** The reference configuration for retry, which every policy spells alike. */
const RETRY_BENCH_PARAMS: Record<string, JsonValue> = {
  baseMs: 100,
  capMs: 10_000,
  maxAttempts: 8,
};

const DOMAINS: Record<string, DomainBench> = {
  cache: {
    traces: Object.keys(CACHE_TRACES),
    primaryMetric: "hitRate",
    primaryTrace: "zipf-1.0-100k",
    run(factory, policy, traceId) {
      const spec = CACHE_TRACES[traceId]!;
      const trace = generateCacheTrace(traceId);
      const options = { capacity: spec.capacity, keyUniverse: spec.keyUniverse };

      // An offline policy declares a `future` parameter; the harness is the only
      // thing that can supply it.
      const wantsFuture = policy.meta.params.some((param) => param.name === "future");
      const params: Record<string, JsonValue> = { capacity: spec.capacity };
      if (wantsFuture) params["future"] = Array.from(trace);

      const build = () =>
        factory(params, undefined as never) as Parameters<typeof runCacheTrace>[0];
      const metrics = cacheMetrics(
        runCacheTrace(build(), trace, options),
      ) as unknown as Record<string, number>;

      return {
        metrics,
        perf: {
          opsPerSec: measureThroughput(trace.length, () => {
            runCacheTrace(build(), trace, options);
          }),
        },
      };
    },
  },

  "rate-limiter": {
    traces: Object.keys(RATE_LIMITER_TRACES),
    primaryMetric: "acceptRate",
    primaryTrace: "steady",
    run(factory, policy, traceId) {
      const spec = RATE_LIMITER_TRACES[traceId]!;
      const trace = generateRateLimiterTrace(traceId);
      const params = RATE_LIMITER_BENCH_PARAMS[policy.id];
      if (params === undefined) {
        throw new Error(
          `${policy.id} has no entry in RATE_LIMITER_BENCH_PARAMS.\n` +
            "Add one mapping the domain's reference budget (100 permits a second, burst 100)\n" +
            "onto this policy's own parameter names. Benching at a policy's defaults would\n" +
            "put a differently configured row in the same table.",
        );
      }

      const options = { keyUniverse: spec.keyUniverse };
      const build = () =>
        factory(params, undefined as never) as Parameters<typeof runRateLimiterTrace>[0];
      const metrics = rateLimiterMetrics(
        runRateLimiterTrace(build(), trace, options),
      ) as unknown as Record<string, number>;
      const opsPerSec = measureThroughput(trace.times.length, () => {
        runRateLimiterTrace(build(), trace, options);
      });

      // `jainFairness` is null on the single-key traces, where fairness is not a
      // meaningful question. A null would not survive the JSON round-trip as a
      // number, so it is dropped rather than written as zero — which would read
      // as "maximally unfair" and be a lie.
      const cleaned: Record<string, number> = {};
      for (const [name, value] of Object.entries(metrics)) {
        if (value !== null && value !== undefined) cleaned[name] = value;
      }

      return { metrics: cleaned, perf: { opsPerSec } };
    },
  },

  "kv-cache": {
    // One row per (trace, budget). The budget is not a policy parameter that
    // can be varied independently — it is the cache size the whole comparison
    // is conditioned on — and the ranking genuinely changes with it (T32 found
    // SnapKV's pooling helping at 256 and 1,024 and hurting at 512). A single
    // reference budget would have hidden that.
    traces: Object.keys(KV_CACHE_TRACES).flatMap((id) =>
      KV_CACHE_BUDGETS.map((budget) => `${id}@${budget}`),
    ),
    primaryMetric: "retainedAttentionMass",
    primaryTrace: `decode-4096@${KV_CACHE_BUDGETS[1]}`,
    run(factory, policy, traceId) {
      void policy;
      const [id, budgetText] = traceId.split("@");
      const spec = KV_CACHE_TRACES[id ?? ""];
      const budget = Number(budgetText);
      if (spec === undefined || !Number.isInteger(budget)) {
        throw new Error(
          `kv-cache bench id "${traceId}" is not <trace>@<budget>. ` +
            `Known traces: ${Object.keys(KV_CACHE_TRACES).join(", ")}`,
        );
      }

      // Every policy in this domain takes `budget`, and the ones that score
      // take `recentWindow` too — but each is benched at its own defaults for
      // everything except the budget, because those defaults are the policy's
      // own claim about how it should be run. The budget is the one thing that
      // must be shared for the row to mean anything.
      const build = () =>
        factory({ budget }, undefined as never) as Parameters<typeof runKvCacheTrace>[0];
      const metrics = kvCacheMetrics(
        runKvCacheTrace(build(), spec, { budget }),
      ) as unknown as Record<string, number>;

      return {
        metrics,
        perf: {
          // Decode steps: the unit of work a policy does, and the analogue of
          // an event in the other domains.
          opsPerSec: measureThroughput(spec.sequenceLength - 1, () => {
            runKvCacheTrace(build(), spec, { budget });
          }),
        },
      };
    },
  },

  retry: {
    traces: Object.keys(RETRY_TRACES),
    primaryMetric: "successRate",
    primaryTrace: "outage-30s",
    run(factory, policy, traceId) {
      void policy;
      const spec = RETRY_TRACES[traceId]!;

      // The harness builds a policy per episode, so each gets a fresh,
      // independently seeded random stream.
      const run = () =>
        runRetryEpisodes((rng) => factory(RETRY_BENCH_PARAMS, rng) as RetryPolicy, spec);

      const result = run();
      const metrics = retryMetrics(result) as unknown as Record<string, number>;

      return {
        metrics,
        perf: {
          // Attempts rather than episodes: it is the unit of work the policy
          // actually does, and the analogue of an event in the other domains.
          opsPerSec: measureThroughput(result.totalAttempts, () => {
            run();
          }),
        },
      };
    },
  },
};

interface Options {
  domains: string[];
  frozenTime: string | undefined;
}

function parseArguments(argv: string[]): Options {
  const domains: string[] = [];
  let frozenTime: string | undefined;

  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index]!;
    // `pnpm bench cache -- --frozen-time X` forwards the separator itself.
    if (argument === "--") continue;
    if (argument === "--frozen-time") {
      frozenTime = argv[index + 1];
      index += 1;
      if (frozenTime === undefined) {
        throw new Error("--frozen-time needs an ISO timestamp");
      }
    } else if (argument === "--all") {
      domains.push(...Object.keys(DOMAINS));
    } else if (argument.startsWith("-")) {
      throw new Error(`unknown option ${argument}`);
    } else {
      domains.push(argument);
    }
  }

  if (domains.length === 0) {
    throw new Error(
      "name a domain (cache) or pass --all.\nUsage: pnpm bench <domain|--all> [-- --frozen-time <iso>]",
    );
  }
  return { domains: [...new Set(domains)], frozenTime };
}

/** Right-pads to a column width, for the printed table. */
function pad(text: string, width: number): string {
  return text.length >= width ? text : text + " ".repeat(width - text.length);
}

function printTable(domain: string, config: DomainBench, rows: Map<string, Record<string, BenchTraceResult>>): void {
  const ids = [...rows.keys()];
  const nameWidth = Math.max(...ids.map((id) => id.length), 8);

  console.log(`\n${domain} — ${config.primaryMetric} by trace\n`);
  console.log(
    `  ${pad("policy", nameWidth)}  ${config.traces.map((trace) => pad(trace, 20)).join("")}`,
  );

  for (const id of ids) {
    const cells = config.traces.map((trace) => {
      const value = rows.get(id)?.[trace]?.metrics[config.primaryMetric];
      return pad(value === undefined ? "-" : value.toFixed(4), 20);
    });
    console.log(`  ${pad(id, nameWidth)}  ${cells.join("")}`);
  }
}

async function main(): Promise<void> {
  const { domains, frozenTime } = parseArguments(process.argv.slice(2));
  const repoRoot = findRepoRoot();
  const all = discoverPolicies(repoRoot);
  const generatedAt = frozenTime ?? new Date().toISOString();

  for (const domain of domains) {
    const config = DOMAINS[domain];
    if (config === undefined) {
      throw new Error(
        `no benchmark harness for domain "${domain}". Known: ${Object.keys(DOMAINS).join(", ")}`,
      );
    }

    const policies = all.filter(
      (policy) => policy.domain === domain && policy.meta.ports.includes("ts"),
    );
    if (policies.length === 0) {
      console.log(`${domain}: no policies with a TypeScript port`);
      continue;
    }

    const rows = new Map<string, Record<string, BenchTraceResult>>();

    for (const policy of policies) {
      const factory = await loadFactory(policy.entry);
      const traces: Record<string, BenchTraceResult> = {};

      for (const traceId of config.traces) {
        process.stdout.write(`  ${policy.id} on ${traceId}\r`);
        traces[traceId] = config.run(factory, policy, traceId);
      }

      rows.set(policy.id, traces);

      /*
       * Keep the recorded throughput unless it has actually moved.
       *
       * `bench.json` is committed, and throughput is a measurement, so writing
       * every run's figure would produce a diff from timing noise alone and
       * bury real changes in churn. Metrics are deterministic and always
       * written; perf is informational and only updated when it moves by more
       * than the noise floor.
       */
      const benchPath = join(policy.dir, "bench.json");
      const previous = existsSync(benchPath)
        ? (JSON.parse(readFileSync(benchPath, "utf8")) as {
            generatedAt: string;
            traces: Record<string, BenchTraceResult>;
          })
        : null;

      if (previous !== null) {
        for (const [trace, result] of Object.entries(traces)) {
          const before = previous.traces[trace]?.perf.opsPerSec;
          if (before === undefined || before === 0) continue;
          const ratio = result.perf.opsPerSec / before;
          if (ratio > 1 - PERF_NOISE && ratio < 1 + PERF_NOISE) {
            result.perf.opsPerSec = before;
          }
        }
      }

      // Only advance the timestamp when something else actually changed.
      const withPreviousStamp = stableJson({
        policy: policy.id,
        generatedAt: previous?.generatedAt ?? generatedAt,
        coreVersion: CORE_VERSION,
        traces,
      });
      const existingText = previous === null ? null : readFileSync(benchPath, "utf8");

      if (existingText !== withPreviousStamp) {
        writeFileSync(
          benchPath,
          stableJson({ policy: policy.id, generatedAt, coreVersion: CORE_VERSION, traces }),
        );
      }
    }

    // Rank by the primary metric on the primary trace, best first, with any
    // offline bound pinned to the end as a reference rather than a competitor.
    const ranked = [...rows.keys()].sort((left, right) => {
      const leftPolicy = all.find((policy) => policy.id === left)!;
      const rightPolicy = all.find((policy) => policy.id === right)!;
      const leftOffline = leftPolicy.meta.status === "offline-bound";
      const rightOffline = rightPolicy.meta.status === "offline-bound";
      if (leftOffline !== rightOffline) return leftOffline ? 1 : -1;

      const leftValue = rows.get(left)?.[config.primaryTrace]?.metrics[config.primaryMetric] ?? 0;
      const rightValue = rows.get(right)?.[config.primaryTrace]?.metrics[config.primaryMetric] ?? 0;
      return rightValue - leftValue;
    });

    const ordered = new Map(ranked.map((id) => [id, rows.get(id)!]));
    printTable(domain, config, ordered);
    console.log(`\n${domain}: wrote ${policies.length} bench.json file(s).`);
  }
}

/** Exposed for the perf guard, which reads the same files. */
export function readBench(policyDir: string): { traces: Record<string, BenchTraceResult> } | null {
  const path = join(policyDir, "bench.json");
  if (!existsSync(path)) return null;
  return JSON.parse(readFileSync(path, "utf8")) as { traces: Record<string, BenchTraceResult> };
}

main().catch((error: unknown) => {
  console.error(error instanceof Error ? error.message : String(error));
  process.exit(1);
});
