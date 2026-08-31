/**
 * Compares measured throughput against recorded baselines.
 *
 * Hit rate is asserted by the vectors and the bench; throughput is not, because
 * it is machine-dependent and CI machines are noisy. But a policy that quietly
 * becomes ten times slower is a real regression, and nothing else in the
 * repository would notice.
 *
 * So this is a *guard*, not a benchmark: it fails only on a collapse, warns on a
 * meaningful slowdown, and says nothing about small movements. It is meant to be
 * run locally, where the baselines were recorded; CI keeps throughput
 * informational.
 *
 * Usage:
 *   pnpm perfguard cache          # check
 *   pnpm perfguard cache --record # re-record baselines on this machine
 */

import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { stableJson } from "../packages/core/src/json";
import { discoverPolicies, findRepoRoot } from "../packages/vectors/src/discover";

interface BenchFile {
  traces: Record<string, { perf: { opsPerSec: number } }>;
}

interface Baselines {
  note: string;
  recordedOn: string;
  policies: Record<string, Record<string, number>>;
}

/** Below this fraction of baseline, fail. */
const FAIL_RATIO = 0.4;
/** Below this fraction of baseline, warn. */
const WARN_RATIO = 0.8;

function main(): void {
  const argv = process.argv.slice(2).filter((argument) => argument !== "--");
  const record = argv.includes("--record");
  const targets = argv.filter((argument) => !argument.startsWith("-"));
  // `--all` was already stripped by the flag filter above, so "no targets
  // named" is what both spellings reduce to.
  const all = targets.length === 0;

  const repoRoot = findRepoRoot();
  const baselinePath = join(repoRoot, "perf-baselines.json");
  const policies = discoverPolicies(repoRoot).filter(
    (policy) => all || targets.includes(policy.domain) || targets.includes(policy.id),
  );

  const existing: Baselines = existsSync(baselinePath)
    ? (JSON.parse(readFileSync(baselinePath, "utf8")) as Baselines)
    : {
        note: "",
        recordedOn: "",
        policies: {},
      };

  const measured: Record<string, Record<string, number>> = {};
  const warnings: string[] = [];
  const failures: string[] = [];
  const missing: string[] = [];
  let checked = 0;

  for (const policy of policies) {
    const path = join(policy.dir, "bench.json");
    if (!existsSync(path)) continue;
    const bench = JSON.parse(readFileSync(path, "utf8")) as BenchFile;

    const perTrace: Record<string, number> = {};
    for (const [trace, result] of Object.entries(bench.traces)) {
      perTrace[trace] = result.perf.opsPerSec;

      const baseline = existing.policies[policy.id]?.[trace];
      if (record) continue;
      if (baseline === undefined) {
        // A measurement without a baseline is not "within tolerance", it is
        // unguarded. Skipping it silently once let a deleted baselines file
        // report "0 measurement(s) within tolerance." and exit 0.
        missing.push(`${policy.id} on ${trace}`);
        continue;
      }

      checked += 1;
      const ratio = result.perf.opsPerSec / baseline;
      const line =
        `${policy.id} on ${trace}: ${result.perf.opsPerSec.toLocaleString("en-US")}/s ` +
        `against a baseline of ${baseline.toLocaleString("en-US")}/s ` +
        `(${(ratio * 100).toFixed(0)}%)`;
      if (ratio < FAIL_RATIO) failures.push(line);
      else if (ratio < WARN_RATIO) warnings.push(line);
    }
    measured[policy.id] = perTrace;
  }

  if (record) {
    const updated: Baselines = {
      note:
        "Throughput baselines for the perf guard. Machine-dependent by nature: these were " +
        "recorded on one machine and are only meaningful there. The guard fails at 40% of " +
        "baseline and warns at 80%, so ordinary variation passes. Re-record with " +
        "`pnpm perfguard <domain> --record`.",
      recordedOn: new Date().toISOString(),
      policies: { ...existing.policies, ...measured },
    };
    writeFileSync(baselinePath, stableJson(updated));
    const count = Object.values(measured).reduce((sum, traces) => sum + Object.keys(traces).length, 0);
    console.log(`perfguard: recorded ${count} baseline(s).`);
    return;
  }

  for (const warning of warnings) console.warn(`warn  ${warning}`);

  if (missing.length > 0) {
    console.error(`\nperfguard: ${missing.length} measurement(s) have no baseline\n`);
    for (const entry of missing) console.error(`  ${entry}`);
    console.error("\nRecord them with `pnpm perfguard <domain> --record`.\n");
    process.exit(1);
  }

  if (checked === 0) {
    console.error("perfguard: nothing was checked — no bench.json matched the targets.");
    process.exit(1);
  }

  if (failures.length > 0) {
    console.error(`\nperfguard: ${failures.length} collapse(s) below ${FAIL_RATIO * 100}% of baseline\n`);
    for (const failure of failures) console.error(`  ${failure}`);
    console.error("");
    process.exit(1);
  }

  const suffix = warnings.length > 0 ? `, ${warnings.length} warning(s)` : "";
  console.log(`perfguard: ${checked} measurement(s) within tolerance${suffix}.`);
}

main();
