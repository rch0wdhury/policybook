/**
 * The decision-parity driver: replay a cache policy and hash every decision.
 *
 * Ports in Python (`tests/test_decision_parity.py`) and C
 * (`tests/cache_decision_parity.c`) implement this loop byte for byte and
 * compare their hashes against the committed `decision-parity.json`. The loop
 * mirrors `runCacheTrace`'s contract — onAccess before insertion, admit
 * consulted on a miss, evict until back within capacity — but records the
 * stream instead of only counting it.
 */

export interface DrivenCachePolicy {
  onAccess(key: number, hit: boolean): void;
  evict(): number;
  admit?(key: number): boolean;
}

export interface DecisionRecord {
  /** FNV-1a64 over the decision stream, as 16 hex digits. */
  hash: string;
  hits: number;
  evictions: number;
  rejections: number;
}

export interface ChurnStreamSpec {
  id: string;
  seed: number;
  capacity: number;
  keyUniverse: number;
  events: number;
}

/**
 * Two deliberately churny small-cache streams. The canonical traces never
 * reached the ghost-promotion-while-full state that split 2Q and S3-FIFO
 * across languages; these streams reach it within a few dozen events.
 */
export const CHURN_STREAMS: readonly ChurnStreamSpec[] = [
  { id: "churn-small", seed: 1, capacity: 4, keyUniverse: 10, events: 20_000 },
  { id: "churn-wide", seed: 4, capacity: 10, keyUniverse: 30, events: 20_000 },
];

/** The numerical-recipes LCG, reduced mod the key universe. */
export function generateChurnStream(spec: ChurnStreamSpec): Uint32Array {
  let state = spec.seed >>> 0;
  const out = new Uint32Array(spec.events);
  for (let index = 0; index < spec.events; index += 1) {
    state = (Math.imul(state, 1664525) + 1013904223) >>> 0;
    out[index] = state % spec.keyUniverse;
  }
  return out;
}

const FNV_OFFSET = 0xcbf29ce484222325n;
const FNV_PRIME = 0x100000001b3n;
const MASK = 0xffffffffffffffffn;

export function driveCachePolicy(
  policy: DrivenCachePolicy,
  trace: Uint32Array,
  capacity: number,
  keyUniverse: number,
): DecisionRecord {
  const resident = new Uint8Array(keyUniverse);
  let residentCount = 0;
  let hits = 0;
  let evictions = 0;
  let rejections = 0;
  let hash = FNV_OFFSET;

  const mix = (byte: number): void => {
    hash = ((hash ^ BigInt(byte)) * FNV_PRIME) & MASK;
  };

  for (let index = 0; index < trace.length; index += 1) {
    const key = trace[index]!;
    const hit = resident[key] === 1;
    policy.onAccess(key, hit);

    if (hit) {
      hits += 1;
      mix(1);
      continue;
    }

    if (policy.admit !== undefined && !policy.admit(key)) {
      rejections += 1;
      mix(2);
      continue;
    }
    mix(0);

    resident[key] = 1;
    residentCount += 1;
    while (residentCount > capacity) {
      const victim = policy.evict();
      if (victim < 0 || victim >= keyUniverse || resident[victim] !== 1) {
        throw new Error(
          `decision parity: policy evicted key ${victim} at event ${index}, which it does not hold`,
        );
      }
      resident[victim] = 0;
      residentCount -= 1;
      evictions += 1;
      mix(victim & 0xff);
      mix((victim >>> 8) & 0xff);
      mix((victim >>> 16) & 0xff);
      mix((victim >>> 24) & 0xff);
    }
  }

  return { hash: hash.toString(16).padStart(16, "0"), hits, evictions, rejections };
}
