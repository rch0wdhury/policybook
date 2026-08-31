/**
 * The synthetic attention generator.
 *
 * Real attention traces would be gigabytes and would tie the registry to one
 * model, so the workload is generated — specified precisely enough that the
 * Python and C ports reproduce it **bit for bit**, which for a float trace is a
 * stronger requirement than anywhere else in this registry (concept.md §10).
 *
 * The mixture is a caricature of what attention actually does, and it is a
 * caricature on purpose: every policy in this domain exists to exploit one of
 * these four components, so a workload that contains all four separates them.
 *
 *   - **sinks** (0.15) on positions 0–3. Real models dump attention on the
 *     first few tokens regardless of content; StreamingLLM is built entirely
 *     around that observation.
 *   - **local** (0.55) on the last 64 positions, weighted toward the most
 *     recent. Recency dominates real attention, which is why a plain sliding
 *     window is a respectable baseline.
 *   - **heavy hitters** (0.25) on 32 scattered positions that persist for a
 *     while and then change. H2O, Scissorhands and TOVA all exist to find
 *     these, and the periodic redraw is what separates a policy that adapts
 *     from one that latches onto an early winner.
 *   - **noise** (0.05) spread over everything, so no position is ever exactly
 *     zero and a policy cannot cheat by testing for it.
 *
 * **Determinism is the hard part here.** Every weight is computed in float64
 * and rounded to float32 at the very end, in a pinned order, using only
 * addition, multiplication and division — no `pow`, no `exp`, no
 * transcendental of any kind, because none of them are correctly rounded
 * across C standard libraries.
 *
 * The float32 rounding is what actually buys bit-exactness: it resolves about
 * one part in 10^7 against float64's 10^-16, so differences in how the float64
 * sum is reached are absorbed rather than propagated. TRACES.md records the
 * fault injections that measured this, and what the parity test does and does
 * not catch as a result. The pinned order below is cheap insurance, not the
 * thing holding parity together.
 *
 * The prose specification is in TRACES.md next to this file; the code and that
 * document must agree.
 */

import { Rng } from "../../rng";

/** Everything needed to reproduce an attention trace. */
export interface KvCacheTraceSpec {
  id: string;
  /** One line, shown on the site and in the CLI. */
  description: string;
  /** Tokens in the sequence. Step `t` attends over positions `0 .. t-1`. */
  sequenceLength: number;
  seed: number;
}

/** The canonical kv-cache workload. */
export const KV_CACHE_TRACES: Record<string, KvCacheTraceSpec> = {
  "decode-4096": {
    id: "decode-4096",
    description:
      "A 4,096-token decode over a mixture of attention sinks, a recency window, 32 shifting heavy hitters and uniform noise. Separates every policy in the domain.",
    sequenceLength: 4_096,
    seed: 7,
  },
};

/** Mass assigned to each component. They sum to one. */
const SINK_MASS = 0.15;
const LOCAL_MASS = 0.55;
const HEAVY_MASS = 0.25;
const NOISE_MASS = 0.05;

/** How the sink mass is split across positions 0–3. Sums to SINK_MASS. */
const SINK_WEIGHTS = [0.06, 0.045, 0.03, 0.015];

/** The recency window: offsets 1..64 back from the current position. */
const LOCAL_SPAN = 64;
/** Offset `d` gets weight `LOCAL_DECAY - d`, so 1 is heaviest and 64 lightest. */
const LOCAL_DECAY = 65;

/** How many heavy hitters are live at once. */
const HEAVY_COUNT = 32;
/**
 * Heavy hitters are drawn from `[4, t - HEAVY_MARGIN - 1]`.
 *
 * The margin keeps them clear of both the sinks and the recency window, so the
 * three components stay separable — a "heavy hitter" inside the local window
 * would be indistinguishable from recency.
 */
const HEAVY_MARGIN = 65;
/**
 * Before this step the heavy component is folded into the local one.
 *
 * At small `t` there is no room between the sinks and the recency window to put
 * 32 distinct scattered positions, so there is nothing to draw from. Folding
 * the mass into local rather than dropping it keeps the total at one.
 */
const HEAVY_START = 128;
/** The heavy set is redrawn at every multiple of this. */
const HEAVY_PERIOD = 512;

function specFor(id: string): KvCacheTraceSpec {
  const spec = KV_CACHE_TRACES[id];
  if (spec === undefined) {
    throw new Error(
      `unknown kv-cache trace "${id}". Known: ${Object.keys(KV_CACHE_TRACES).join(", ")}`,
    );
  }
  return spec;
}

/**
 * Draw a fresh set of heavy-hitter positions.
 *
 * Rejection sampling with a pinned call order: draw, and if the position is
 * already in the set, draw again. Both the number of draws and their order
 * matter, because the rank of a position in the set decides its weight.
 */
function drawHeavy(rng: Rng, t: number, into: number[]): void {
  into.length = 0;
  // Positions run from 4 (clear of the sinks) to `t - HEAVY_MARGIN` (just
  // outside the recency window, which at step t covers `t-64 .. t-1`), so the
  // draw is `nextInt(span) + 4` over that many candidates.
  const span = t - HEAVY_MARGIN - 4 + 1;
  if (span < HEAVY_COUNT) return;

  const seen = new Set<number>();
  while (into.length < HEAVY_COUNT) {
    const position = rng.nextInt(span) + 4;
    if (seen.has(position)) continue;
    seen.add(position);
    into.push(position);
  }
}

/**
 * The attention weights for one decode step, over positions `0 .. t-1`.
 *
 * Contributions are accumulated in a fixed order — sink, local, heavy, noise —
 * because float addition is not associative and a different order would give a
 * different last bit. Then one division per position normalises, and one
 * `fround` per position takes it to float32.
 */
function stepWeights(t: number, heavy: number[], out: Float64Array): Float32Array {
  out.fill(0, 0, t);

  // Sinks. When fewer than four positions exist the available weights are
  // scaled so the component still contributes exactly SINK_MASS.
  const sinks = Math.min(4, t);
  let sinkTotal = 0;
  for (let i = 0; i < sinks; i += 1) sinkTotal += SINK_WEIGHTS[i]!;
  for (let i = 0; i < sinks; i += 1) {
    out[i] = out[i]! + (SINK_WEIGHTS[i]! * SINK_MASS) / sinkTotal;
  }

  // Local: offsets 1..min(64, t) back from t, weighted toward the recent.
  const span = Math.min(LOCAL_SPAN, t);
  let localWeight = 0;
  for (let d = 1; d <= span; d += 1) localWeight += LOCAL_DECAY - d;

  // Below HEAVY_START the heavy component has nowhere to live, so its mass
  // joins the local one and the total still comes to one.
  const localMass = t < HEAVY_START ? LOCAL_MASS + HEAVY_MASS : LOCAL_MASS;
  for (let d = 1; d <= span; d += 1) {
    const position = t - d;
    out[position] = out[position]! + ((LOCAL_DECAY - d) * localMass) / localWeight;
  }

  // Heavy hitters, weighted by draw order: the first drawn is heaviest.
  if (t >= HEAVY_START && heavy.length > 0) {
    let heavyWeight = 0;
    for (let rank = 0; rank < heavy.length; rank += 1) heavyWeight += 1 / (rank + 1);
    for (let rank = 0; rank < heavy.length; rank += 1) {
      const position = heavy[rank]!;
      out[position] = out[position]! + (HEAVY_MASS / (rank + 1)) / heavyWeight;
    }
  }

  // Noise, so no position is ever exactly zero.
  const perPosition = NOISE_MASS / t;
  for (let i = 0; i < t; i += 1) out[i] = out[i]! + perPosition;

  // Normalise, then take each weight to float32. The total is one to within
  // rounding already, and removing this division was measured to change no
  // output bit — it stays because it makes the invariant exact in float64 too.
  let total = 0;
  for (let i = 0; i < t; i += 1) total += out[i]!;

  const weights = new Float32Array(t);
  for (let i = 0; i < t; i += 1) weights[i] = Math.fround(out[i]! / total);
  return weights;
}

/**
 * Generate the attention weights of every decode step.
 *
 * Step `t` (from 1) yields a `Float32Array` of length `t`: the attention the
 * token at position `t` paid to each earlier position. Steps are produced in
 * order because the heavy-hitter set carries between them.
 *
 * The last step is `sequenceLength - 1`, not `sequenceLength`: the attending
 * token at step `t` sits at position `t`, and the final position of an N-token
 * sequence is `N - 1`. Position 0's token exists before decoding starts and
 * never attends.
 *
 * @param id one of {@link KV_CACHE_TRACES}.
 * @param maxSteps stop after this many steps. The generator is sequential and
 *   consumes the random stream in order, so a truncated run is exactly a prefix
 *   of the full one.
 */
export function* generateKvCacheTrace(
  id: string,
  maxSteps?: number,
): Generator<Float32Array, void, undefined> {
  const spec = specFor(id);
  const lastStep = spec.sequenceLength - 1;
  const steps = maxSteps === undefined ? lastStep : Math.min(maxSteps, lastStep);

  const rng = new Rng(spec.seed);
  const heavy: number[] = [];
  // Scratch space, reused across steps so a 4,096-step run allocates one
  // float64 buffer rather than four thousand.
  const scratch = new Float64Array(spec.sequenceLength);

  for (let t = 1; t <= steps; t += 1) {
    // The set is drawn when first needed and redrawn on the period. Both
    // conditions are pinned: a port that redrew on a different step would
    // diverge from here on, and the parity test would say exactly where.
    if (t === HEAVY_START || (t > HEAVY_START && t % HEAVY_PERIOD === 0)) {
      drawHeavy(rng, t, heavy);
    }
    yield stepWeights(t, heavy, scratch);
  }
}

/**
 * The float32 bit pattern of a weight, as an unsigned 32-bit integer.
 *
 * Comparing bits rather than values is what makes the parity check exact: two
 * floats that print the same can still differ in the last place, and for a
 * generator whose whole job is to be reproducible that difference is the bug.
 */
export function float32Bits(value: number): number {
  FLOAT_VIEW[0] = value;
  return BITS_VIEW[0]!;
}

const FLOAT_VIEW = new Float32Array(1);
const BITS_VIEW = new Uint32Array(FLOAT_VIEW.buffer);

/**
 * FNV-1a 32 over the float32 bit patterns of every step.
 *
 * The parity artefact for this domain. Committing whole traces would mean eight
 * million floats; a hash says the same thing in one number, and the committed
 * first steps say *where* a divergence starts. Thirty-two bits is ample for
 * detecting a change rather than searching for one.
 *
 * Each weight contributes its four bytes little-endian, so a port on a
 * big-endian machine still hashes the same bytes in the same order.
 */
export function hashKvCacheTrace(id: string, maxSteps?: number): number {
  let hash = 0x811c_9dc5;

  for (const step of generateKvCacheTrace(id, maxSteps)) {
    for (let i = 0; i < step.length; i += 1) {
      const bits = float32Bits(step[i]!);
      for (let byte = 0; byte < 4; byte += 1) {
        hash = (hash ^ ((bits >>> (byte * 8)) & 0xff)) >>> 0;
        hash = Math.imul(hash, 0x0100_0193) >>> 0;
      }
    }
  }

  return hash >>> 0;
}
