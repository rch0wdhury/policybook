/**
 * Canonical traces for the cache domain.
 *
 * A benchmark is only meaningful if everyone runs the same workload, so traces
 * are generated rather than downloaded: specified precisely enough that the
 * Python and C ports reproduce them event for event from the same seed
 * (concept.md §10). Nothing here reads a file, a clock, or an environment
 * variable.
 *
 * The full prose specification is in TRACES.md next to this file; the code and
 * that document must agree.
 */

import { Rng } from "../../rng";
import { ZipfSampler } from "../../zipf";
import type { ZipfAlpha } from "../../zipf";

/** Everything needed to reproduce a trace and to benchmark against it. */
export interface CacheTraceSpec {
  id: string;
  /** One line, shown on the site and in the CLI. */
  description: string;
  /** Number of distinct keys the Zipf body draws from. */
  keyspace: number;
  /** Exclusive upper bound on any key the trace emits. */
  keyUniverse: number;
  /** Cache capacity this trace is benchmarked at. */
  capacity: number;
  /** Total events emitted. */
  events: number;
  seed: number;
}

/** The four canonical cache traces. */
export const CACHE_TRACES: Record<string, CacheTraceSpec> = {
  "zipf-1.0-100k": {
    id: "zipf-1.0-100k",
    description:
      "100,000 accesses over 10,000 keys, Zipf alpha 1.0. The everyday skewed workload.",
    keyspace: 10_000,
    keyUniverse: 10_000,
    capacity: 1_000,
    events: 100_000,
    seed: 42,
  },
  "zipf-0.75-1m": {
    id: "zipf-0.75-1m",
    description:
      "1,000,000 accesses over 100,000 keys, Zipf alpha 0.75. Flatter, and large enough to punish per-entry overhead.",
    keyspace: 100_000,
    keyUniverse: 100_000,
    capacity: 10_000,
    events: 1_000_000,
    seed: 43,
  },
  "scan-heavy": {
    id: "scan-heavy",
    description:
      "Zipf 1.0 with a sequential scan of twice the capacity injected every 20,000 accesses. Separates scan-resistant policies from LRU.",
    keyspace: 10_000,
    // The scans emit keys above the Zipf keyspace: 10,000 .. 17,999.
    keyUniverse: 18_000,
    capacity: 1_000,
    events: 108_000,
    seed: 44,
  },
  "shifting-popularity": {
    id: "shifting-popularity",
    description:
      "Zipf 1.0 whose popular set rotates every 25,000 accesses. Punishes policies that cannot forget.",
    keyspace: 10_000,
    keyUniverse: 10_000,
    capacity: 1_000,
    events: 100_000,
    seed: 45,
  },
};

/**
 * How often the scan-heavy trace injects a scan.
 *
 * Exported because anything that runs a *prefix* of this trace has to know it.
 * A prefix shorter than one interval contains no scan at all, and so is not the
 * scan-heavy trace — it is plain Zipf wearing its name. The site runner uses
 * this to pick a prefix that still shows the behaviour.
 */
export const SCAN_INTERVAL = 20_000;
/** How far the popular set rotates each time, in keys. */
const POPULARITY_SHIFT = 2_500;
/** How often the popular set rotates. Exported for the same reason as SCAN_INTERVAL. */
export const SHIFT_INTERVAL = 25_000;

function specFor(id: string): CacheTraceSpec {
  const spec = CACHE_TRACES[id];
  if (spec === undefined) {
    throw new Error(
      `unknown cache trace "${id}". Known: ${Object.keys(CACHE_TRACES).join(", ")}`,
    );
  }
  return spec;
}

/**
 * Generate a trace as a flat array of keys.
 *
 * @param id one of {@link CACHE_TRACES}.
 * @param maxEvents stop early after this many events. The site uses a reduced
 *   length for responsiveness (concept.md §13.6); the prefix of a trace is
 *   identical to the full trace, so a short run is still reproducible.
 */
export function generateCacheTrace(id: string, maxEvents?: number): Uint32Array {
  const spec = specFor(id);
  const limit = maxEvents === undefined ? spec.events : Math.min(maxEvents, spec.events);

  switch (id) {
    case "zipf-1.0-100k":
      return generateZipf(spec, 1, limit);
    case "zipf-0.75-1m":
      return generateZipf(spec, 0.75, limit);
    case "scan-heavy":
      return generateScanHeavy(spec, limit);
    case "shifting-popularity":
      return generateShiftingPopularity(spec, limit);
    default:
      throw new Error(`no generator for trace "${id}"`);
  }
}

/** Plain Zipf: draw a rank, emit it as the key. */
function generateZipf(spec: CacheTraceSpec, alpha: ZipfAlpha, limit: number): Uint32Array {
  const rng = new Rng(spec.seed);
  const zipf = new ZipfSampler(spec.keyspace, alpha);
  const trace = new Uint32Array(limit);

  for (let index = 0; index < limit; index += 1) {
    trace[index] = zipf.sample(rng);
  }
  return trace;
}

/**
 * Zipf with periodic sequential scans.
 *
 * Every SCAN_INTERVAL accesses, a run of 2 x capacity fresh keys sweeps
 * through — the shape of a backup, a table scan, or a crawler. LRU evicts its
 * entire working set to make room for keys it will never see again; a
 * scan-resistant policy barely notices. Scan keys live above the Zipf keyspace
 * so they can never collide with the working set.
 */
function generateScanHeavy(spec: CacheTraceSpec, limit: number): Uint32Array {
  const rng = new Rng(spec.seed);
  const zipf = new ZipfSampler(spec.keyspace, 1);
  const trace = new Uint32Array(limit);
  const scanLength = spec.capacity * 2;

  let written = 0;
  let scanIndex = 0;

  for (let step = 0; step < spec.events && written < limit; step += 1) {
    if (step > 0 && step % SCAN_INTERVAL === 0) {
      const scanBase = spec.keyspace + scanIndex * scanLength;
      for (let offset = 0; offset < scanLength && written < limit; offset += 1) {
        trace[written] = scanBase + offset;
        written += 1;
      }
      scanIndex += 1;
      if (written >= limit) break;
    }

    trace[written] = zipf.sample(rng);
    written += 1;
  }

  return written === limit ? trace : trace.subarray(0, written);
}

/**
 * Zipf whose popular set rotates.
 *
 * The rank drawn is the same as ever, but it is offset by a rotation that
 * advances every SHIFT_INTERVAL accesses, so yesterday's hot keys go cold.
 * Frequency-based policies that never forget keep the wrong entries.
 */
function generateShiftingPopularity(spec: CacheTraceSpec, limit: number): Uint32Array {
  const rng = new Rng(spec.seed);
  const zipf = new ZipfSampler(spec.keyspace, 1);
  const trace = new Uint32Array(limit);

  for (let step = 0; step < limit; step += 1) {
    const rank = zipf.sample(rng);
    const shift = Math.floor(step / SHIFT_INTERVAL) * POPULARITY_SHIFT;
    trace[step] = (rank + shift) % spec.keyspace;
  }
  return trace;
}
