/**
 * W-TinyLFU — frequency-based admission on a budget of four bits per counter.
 *
 * [LFU](../lfu/) keeps an exact count for every entry, which is expensive and
 * never forgets. W-TinyLFU keeps *approximate* counts for far more keys than it
 * caches, in a fixed-size sketch, and halves them periodically so the estimate
 * follows the workload. That makes frequency information cheap enough to use as
 * an **admission** test rather than only an eviction rule.
 *
 * The cache is in two parts. A small **window** LRU (1% of capacity) absorbs
 * new arrivals, which is what lets a burst of related requests hit at all. When
 * the window overflows, its victim becomes a *candidate*: its estimated
 * frequency is compared against that of the main cache's next victim, and only
 * the more popular of the two survives. The main cache is a segmented LRU —
 * entries enter on **probation** and are promoted to **protected** on a second
 * hit — so the truly popular keys sit furthest from eviction.
 *
 * Two tricks make the sketch small enough to be free. Counters are four bits,
 * two to a byte, so 15 is "very popular" and there is no need for more. And a
 * **doorkeeper** bloom filter absorbs each key's first appearance, so the
 * enormous number of keys seen exactly once never reach the sketch at all.
 *
 * Keys must be integers: the sketch hashes them directly, and a string key
 * would need a string hash defined identically in three languages. Callers with
 * other key types hash their own, exactly as the C API expects.
 */

// The extension is required: a policy file is imported directly by Node, which
// strips types but resolves specifiers exactly as written. Sharing `mix32` with
// the core rather than inlining it is what keeps this sketch's hashing
// identical to the Python and C ports.
import { mix32 } from "../../../packages/core/src/rng.ts";

export interface WTinyLfuParams {
  /** Maximum number of entries held. */
  capacity: number;
  /** Fraction of capacity given to the admission window. */
  windowFraction: number;
  /** Fraction of the main cache reserved for protected entries. */
  protectedFraction: number;
}

const DEFAULT_CAPACITY = 1000;
const DEFAULT_WINDOW_FRACTION = 0.01;
const DEFAULT_PROTECTED_FRACTION = 0.8;

const NIL = -1;

/** Lists, used to index the head/tail/length arrays. */
const WINDOW = 0;
const PROBATION = 1;
const PROTECTED = 2;

/** Row salts for the sketch. Arbitrary odd constants with good bit mixing. */
const SALT = [0x9e3779b9, 0x85ebca6b, 0xc2b2ae35, 0x27d4eb2f];
const SKETCH_ROWS = 4;
/** Counters saturate here; four bits cannot hold more. */
const MAX_COUNT = 15;

/** Where an entry sits. Returned by {@link WTinyLfu.segmentOf}. */
export type WTinyLfuSegment = "window" | "probation" | "protected" | "absent";
const SEGMENT_NAMES: WTinyLfuSegment[] = ["window", "probation", "protected"];

export default class WTinyLfu {
  private readonly capacity: number;
  private readonly windowSize: number;
  private readonly mainSize: number;
  private readonly protectedSize: number;

  private readonly index: Map<number, number>;
  private readonly keys: Float64Array;

  // Three lists over one slot pool; an entry is in exactly one of them.
  private readonly next: Int32Array;
  private readonly prev: Int32Array;
  private readonly listOf: Int8Array;
  private readonly heads: Int32Array;
  private readonly tails: Int32Array;
  private readonly lengths: Int32Array;

  private readonly freeSlots: Int32Array;
  private freeCount: number;

  // --- the frequency sketch -------------------------------------------------
  /** Four-bit counters, two to a byte. */
  private readonly sketch: Uint8Array;
  private readonly sketchWidth: number;
  private readonly sketchMask: number;
  /** One bit per position, absorbing every key's first appearance. */
  private readonly doorkeeper: Uint8Array;
  private readonly doorkeeperBits: number;
  private readonly doorkeeperMask: number;
  /** Accesses since the last halving. */
  private sampled = 0;
  private readonly sampleLimit: number;

  constructor(params: Partial<WTinyLfuParams> = {}) {
    const capacity = params.capacity ?? DEFAULT_CAPACITY;
    const windowFraction = params.windowFraction ?? DEFAULT_WINDOW_FRACTION;
    const protectedFraction = params.protectedFraction ?? DEFAULT_PROTECTED_FRACTION;

    if (!Number.isInteger(capacity) || capacity < 2) {
      throw new RangeError(
        `WTinyLfu: capacity must be an integer of at least 2, received ${capacity}. ` +
          "The window and the main cache each need at least one entry.",
      );
    }
    if (!(windowFraction > 0) || windowFraction >= 1) {
      throw new RangeError(
        `WTinyLfu: windowFraction must be in (0, 1), received ${windowFraction}`,
      );
    }
    if (!(protectedFraction > 0) || protectedFraction >= 1) {
      throw new RangeError(
        `WTinyLfu: protectedFraction must be in (0, 1), received ${protectedFraction}`,
      );
    }

    this.capacity = capacity;
    // The window holds at least one entry, and never the whole cache.
    this.windowSize = Math.max(1, Math.floor(capacity * windowFraction));
    this.mainSize = capacity - this.windowSize;
    this.protectedSize = Math.max(1, Math.floor(this.mainSize * protectedFraction));

    const slots = capacity + 1;
    this.index = new Map<number, number>();
    this.keys = new Float64Array(slots);
    this.next = new Int32Array(slots).fill(NIL);
    this.prev = new Int32Array(slots).fill(NIL);
    this.listOf = new Int8Array(slots).fill(-1);
    this.heads = new Int32Array(3).fill(NIL);
    this.tails = new Int32Array(3).fill(NIL);
    this.lengths = new Int32Array(3);
    this.freeSlots = new Int32Array(slots);
    for (let slot = 0; slot < slots; slot += 1) this.freeSlots[slot] = slots - 1 - slot;
    this.freeCount = slots;

    // Eight sketch positions per cached entry, rounded up to a power of two so
    // the modulo is a mask.
    this.sketchWidth = nextPowerOfTwo(capacity * 8);
    this.sketchMask = this.sketchWidth - 1;
    this.sketch = new Uint8Array((SKETCH_ROWS * this.sketchWidth) >> 1);

    this.doorkeeperBits = nextPowerOfTwo(capacity * 8);
    this.doorkeeperMask = this.doorkeeperBits - 1;
    this.doorkeeper = new Uint8Array(this.doorkeeperBits >> 3);

    // Halve the counters every ten accesses per cached entry, so the estimate
    // tracks the workload instead of accumulating forever.
    this.sampleLimit = capacity * 10;
  }

  onAccess(key: number, hit: boolean): void {
    // Every access is evidence, whether or not the key is resident. Counting
    // misses too is what lets a key earn admission before it is ever cached.
    this.record(key);

    if (hit) {
      const slot = this.index.get(key);
      if (slot === undefined) {
        throw new Error(`WTinyLfu: onAccess reported a hit for a key it does not hold: ${key}`);
      }

      const list = this.listOf[slot]!;
      if (list === WINDOW) {
        this.moveToFront(WINDOW, slot);
        return;
      }
      if (list === PROBATION) {
        // A second hit in the main cache promotes the entry out of reach of
        // the admission contest.
        this.unlink(slot);
        this.linkFront(PROTECTED, slot);
        if (this.lengths[PROTECTED]! > this.protectedSize) {
          const demoted = this.tails[PROTECTED]!;
          this.unlink(demoted);
          this.linkFront(PROBATION, demoted);
        }
        return;
      }
      this.moveToFront(PROTECTED, slot);
      return;
    }

    if (this.freeCount === 0) {
      throw new Error(
        `WTinyLfu: ${this.capacity + 1} entries inserted without an evict, capacity is ` +
          `${this.capacity}. Call evict() once the cache is over capacity.`,
      );
    }

    // New keys always enter the window. Admission is decided later, when the
    // window overflows and the key has to argue for a place in the main cache.
    this.freeCount -= 1;
    const slot = this.freeSlots[this.freeCount]!;
    this.keys[slot] = key;
    this.index.set(key, slot);
    this.linkFront(WINDOW, slot);

    // The window keeps its size continuously, not only when the cache is full.
    // While the main cache has room the overflow simply moves across, which
    // costs nothing and needs no contest; once the main cache is full the
    // window is left over its size and `evict` runs the admission contest.
    this.drainWindow();
  }

  evict(): number {
    // Normally a no-op here: the window is drained on insertion. It remains as
    // a guard for a caller that inserts several keys before evicting.
    this.drainWindow();

    if (this.lengths[WINDOW]! > this.windowSize) {
      // The admission contest. The window's victim has to be more popular than
      // the main cache's victim to take its place.
      const candidate = this.tails[WINDOW]!;
      const victim = this.mainVictim();

      const candidateFrequency = this.estimate(this.keys[candidate]!);
      const victimFrequency = this.estimate(this.keys[victim]!);

      // Strictly greater: on a tie the incumbent stays. A resident entry has
      // demonstrated its frequency, while the candidate has only an estimate,
      // and admitting on equal evidence would let a stream of one-hit wonders
      // churn the cache.
      if (candidateFrequency > victimFrequency) {
        this.unlink(candidate);
        this.linkFront(PROBATION, candidate);
        return this.release(victim);
      }
      return this.release(candidate);
    }

    return this.release(this.mainVictim());
  }

  /** Entries currently held. Not part of the interface; used by tests. */
  size(): number {
    return this.index.size;
  }

  /** Which segment a key is in. Not part of the interface; used by tests. */
  segmentOf(key: number): WTinyLfuSegment {
    const slot = this.index.get(key);
    if (slot === undefined) return "absent";
    return SEGMENT_NAMES[this.listOf[slot]!] ?? "absent";
  }

  /** The sketch's estimate for a key. Not part of the interface; used by tests. */
  frequencyOf(key: number): number {
    return this.estimate(key);
  }

  // --- the sketch -----------------------------------------------------------

  /**
   * Note an access to `key`.
   *
   * The doorkeeper absorbs a key's first appearance, so the very large number
   * of keys seen exactly once never consume sketch counters at all. Only from
   * the second appearance does a key start accumulating a count.
   */
  private record(key: number): void {
    const hash = mix32(key);

    if (!this.doorkeeperTest(hash)) {
      this.doorkeeperSet(hash);
    } else {
      for (let row = 0; row < SKETCH_ROWS; row += 1) {
        this.incrementCounter(row * this.sketchWidth + this.position(hash, row));
      }
    }

    this.sampled += 1;
    if (this.sampled >= this.sampleLimit) this.age();
  }

  /** The count-min estimate, plus one if the doorkeeper has seen the key. */
  private estimate(key: number): number {
    const hash = mix32(key);

    let smallest = MAX_COUNT;
    for (let row = 0; row < SKETCH_ROWS; row += 1) {
      const count = this.counterAt(row * this.sketchWidth + this.position(hash, row));
      if (count < smallest) smallest = count;
    }

    return this.doorkeeperTest(hash) ? smallest + 1 : smallest;
  }

  private position(hash: number, row: number): number {
    return mix32((hash ^ SALT[row]!) >>> 0) & this.sketchMask;
  }

  private counterAt(index: number): number {
    const byte = this.sketch[index >> 1]!;
    return (index & 1) === 0 ? byte & 0x0f : byte >>> 4;
  }

  private incrementCounter(index: number): void {
    const byteIndex = index >> 1;
    const byte = this.sketch[byteIndex]!;
    if ((index & 1) === 0) {
      const value = byte & 0x0f;
      if (value < MAX_COUNT) this.sketch[byteIndex] = (byte & 0xf0) | (value + 1);
    } else {
      const value = byte >>> 4;
      if (value < MAX_COUNT) this.sketch[byteIndex] = (byte & 0x0f) | ((value + 1) << 4);
    }
  }

  /**
   * Halve every counter and forget the doorkeeper.
   *
   * This is what stops W-TinyLFU becoming LFU: a key that was popular an hour
   * ago decays instead of holding its place forever. Shifting each nibble right
   * and masking with 0x77 halves both counters in a byte at once, without
   * letting the high nibble's low bit leak into the low one.
   */
  private age(): void {
    for (let byte = 0; byte < this.sketch.length; byte += 1) {
      this.sketch[byte] = (this.sketch[byte]! >>> 1) & 0x77;
    }
    this.doorkeeper.fill(0);
    this.sampled = 0;
  }

  private doorkeeperPosition(hash: number, which: number): number {
    return mix32((hash ^ SALT[which]!) >>> 0) & this.doorkeeperMask;
  }

  private doorkeeperTest(hash: number): boolean {
    for (let which = 0; which < 2; which += 1) {
      const bit = this.doorkeeperPosition(hash, which);
      if ((this.doorkeeper[bit >> 3]! & (1 << (bit & 7))) === 0) return false;
    }
    return true;
  }

  private doorkeeperSet(hash: number): void {
    for (let which = 0; which < 2; which += 1) {
      const bit = this.doorkeeperPosition(hash, which);
      this.doorkeeper[bit >> 3] = this.doorkeeper[bit >> 3]! | (1 << (bit & 7));
    }
  }

  // --- lists ----------------------------------------------------------------

  private mainLength(): number {
    return this.lengths[PROBATION]! + this.lengths[PROTECTED]!;
  }

  /** Move the window's overflow into the main cache while it has room. */
  private drainWindow(): void {
    while (this.lengths[WINDOW]! > this.windowSize && this.mainLength() < this.mainSize) {
      const promoted = this.tails[WINDOW]!;
      this.unlink(promoted);
      this.linkFront(PROBATION, promoted);
    }
  }

  /** The main cache's next victim: probation's oldest, or protected's. */
  private mainVictim(): number {
    const probation = this.tails[PROBATION]!;
    if (probation !== NIL) return probation;
    const guarded = this.tails[PROTECTED]!;
    if (guarded !== NIL) return guarded;
    const windowed = this.tails[WINDOW]!;
    if (windowed !== NIL) return windowed;
    throw new Error("WTinyLfu: evict() called with nothing resident");
  }

  private release(slot: number): number {
    const key = this.keys[slot]!;
    this.unlink(slot);
    this.index.delete(key);
    this.freeSlots[this.freeCount] = slot;
    this.freeCount += 1;
    return key;
  }

  private linkFront(list: number, slot: number): void {
    const head = this.heads[list]!;
    this.prev[slot] = NIL;
    this.next[slot] = head;
    if (head !== NIL) this.prev[head] = slot;
    else this.tails[list] = slot;
    this.heads[list] = slot;
    this.listOf[slot] = list;
    this.lengths[list] = this.lengths[list]! + 1;
  }

  private unlink(slot: number): void {
    const list = this.listOf[slot]!;
    const next = this.next[slot]!;
    const prev = this.prev[slot]!;

    if (prev !== NIL) this.next[prev] = next;
    else this.heads[list] = next;
    if (next !== NIL) this.prev[next] = prev;
    else this.tails[list] = prev;

    this.next[slot] = NIL;
    this.prev[slot] = NIL;
    this.listOf[slot] = -1;
    this.lengths[list] = this.lengths[list]! - 1;
  }

  private moveToFront(list: number, slot: number): void {
    if (this.heads[list] === slot) return;
    this.unlink(slot);
    this.linkFront(list, slot);
  }
}

function nextPowerOfTwo(value: number): number {
  let result = 1;
  while (result < value) result *= 2;
  return result;
}
