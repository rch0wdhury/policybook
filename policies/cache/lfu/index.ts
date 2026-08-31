/**
 * LFU — evict the key used least often.
 *
 * Where LRU bets on recency, LFU bets on popularity: a key accessed a hundred
 * times is kept over one accessed twice, however long ago the hundred were. On
 * a stable, skewed workload that is the better bet, and LFU beats LRU
 * comfortably. When popularity shifts, it is the worse bet, and LFU holds
 * yesterday's winners forever — it has no way to forget.
 *
 * The implementation is the O(1) design of Shah, Mitra and Matani (2010).
 * The naive version keeps a counter per entry and scans for the minimum, which
 * is O(n) per eviction. Instead, entries are grouped into **frequency classes**,
 * each holding every entry with the same count, and the classes themselves form
 * an ascending linked list. Promoting an entry moves it to the neighbouring
 * class; evicting takes from the first class. Neither ever scans.
 *
 * Both the entry lists and the class list live in `Int32Array`s indexed by slot,
 * so nothing allocates after construction.
 */

export interface LfuParams {
  /** Maximum number of entries held. */
  capacity: number;
}

const DEFAULT_CAPACITY = 1000;
const NIL = -1;

export default class Lfu<K> {
  private readonly capacity: number;

  /** key -> entry slot. */
  private readonly index: Map<K, number>;
  private readonly keys: (K | undefined)[];

  // Entry links, within a frequency class.
  private readonly entryNext: Int32Array;
  private readonly entryPrev: Int32Array;
  private readonly entryClass: Int32Array;
  private readonly freeEntries: Int32Array;
  private freeEntryCount: number;

  // Frequency classes, kept in ascending order of frequency.
  private readonly classFreq: Int32Array;
  private readonly classHead: Int32Array;
  private readonly classTail: Int32Array;
  private readonly classNext: Int32Array;
  private readonly classPrev: Int32Array;
  private readonly freeClasses: Int32Array;
  private freeClassCount: number;
  /** First class in ascending order: the eviction candidates. */
  private classListHead = NIL;

  constructor(params: Partial<LfuParams> = {}) {
    const capacity = params.capacity ?? DEFAULT_CAPACITY;
    if (!Number.isInteger(capacity) || capacity < 1) {
      throw new RangeError(`Lfu: capacity must be a positive integer, received ${capacity}`);
    }

    this.capacity = capacity;
    // One spare: a caller inserts before evicting, so the cache is briefly one
    // over capacity. There can never be more distinct frequencies than entries.
    const slots = capacity + 1;

    this.index = new Map<K, number>();
    this.keys = new Array<K | undefined>(slots);

    this.entryNext = new Int32Array(slots).fill(NIL);
    this.entryPrev = new Int32Array(slots).fill(NIL);
    this.entryClass = new Int32Array(slots).fill(NIL);
    this.freeEntries = new Int32Array(slots);
    for (let slot = 0; slot < slots; slot += 1) this.freeEntries[slot] = slots - 1 - slot;
    this.freeEntryCount = slots;

    this.classFreq = new Int32Array(slots);
    this.classHead = new Int32Array(slots).fill(NIL);
    this.classTail = new Int32Array(slots).fill(NIL);
    this.classNext = new Int32Array(slots).fill(NIL);
    this.classPrev = new Int32Array(slots).fill(NIL);
    this.freeClasses = new Int32Array(slots);
    for (let slot = 0; slot < slots; slot += 1) this.freeClasses[slot] = slots - 1 - slot;
    this.freeClassCount = slots;
  }

  onAccess(key: K, hit: boolean): void {
    if (hit) {
      const entry = this.index.get(key);
      if (entry === undefined) {
        throw new Error(`Lfu: onAccess reported a hit for a key it does not hold: ${String(key)}`);
      }
      this.promote(entry);
      return;
    }

    if (this.freeEntryCount === 0) {
      throw new Error(
        `Lfu: ${this.capacity + 1} entries inserted without an evict, capacity is ${this.capacity}. ` +
          "Call evict() once the cache is over capacity.",
      );
    }

    this.freeEntryCount -= 1;
    const entry = this.freeEntries[this.freeEntryCount]!;
    this.keys[entry] = key;
    this.index.set(key, entry);

    // A new entry has frequency 1, so it belongs at the front of the class list.
    const first = this.classListHead;
    const target =
      first !== NIL && this.classFreq[first] === 1 ? first : this.insertClassBefore(first, 1);
    this.appendEntry(target, entry);
  }

  evict(): K {
    if (this.classListHead === NIL) {
      throw new Error("Lfu: evict() called with nothing resident");
    }

    // The first class holds the least frequently used entries; its head is the
    // one that reached that frequency earliest.
    const klass = this.classListHead;
    const entry = this.classHead[klass]!;
    const key = this.keys[entry] as K;

    this.removeEntry(klass, entry);
    this.index.delete(key);
    this.keys[entry] = undefined;
    this.freeEntries[this.freeEntryCount] = entry;
    this.freeEntryCount += 1;

    return key;
  }

  /** Entries currently held. Not part of the interface; used by tests. */
  size(): number {
    return this.index.size;
  }

  /** The access count recorded for a key, or 0. Used by tests. */
  frequencyOf(key: K): number {
    const entry = this.index.get(key);
    if (entry === undefined) return 0;
    return this.classFreq[this.entryClass[entry]!]!;
  }

  private promote(entry: number): void {
    const from = this.entryClass[entry]!;
    const frequency = this.classFreq[from]!;
    const after = this.classNext[from]!;

    // Reuse the neighbouring class if it is already the frequency we want.
    const target =
      after !== NIL && this.classFreq[after] === frequency + 1
        ? after
        : this.insertClassAfter(from, frequency + 1);

    this.removeEntry(from, entry);
    this.appendEntry(target, entry);
  }

  private appendEntry(klass: number, entry: number): void {
    this.entryClass[entry] = klass;
    this.entryNext[entry] = NIL;
    this.entryPrev[entry] = this.classTail[klass]!;

    const tail = this.classTail[klass]!;
    if (tail !== NIL) this.entryNext[tail] = entry;
    else this.classHead[klass] = entry;
    this.classTail[klass] = entry;
  }

  private removeEntry(klass: number, entry: number): void {
    const next = this.entryNext[entry]!;
    const prev = this.entryPrev[entry]!;

    if (prev !== NIL) this.entryNext[prev] = next;
    else this.classHead[klass] = next;
    if (next !== NIL) this.entryPrev[next] = prev;
    else this.classTail[klass] = prev;

    this.entryNext[entry] = NIL;
    this.entryPrev[entry] = NIL;
    this.entryClass[entry] = NIL;

    // An empty class carries no information and must not be left in the list,
    // or eviction would find nothing in it.
    if (this.classHead[klass] === NIL) this.releaseClass(klass);
  }

  private allocateClass(frequency: number): number {
    if (this.freeClassCount === 0) {
      throw new Error("Lfu: ran out of frequency classes, which should be impossible");
    }
    this.freeClassCount -= 1;
    const klass = this.freeClasses[this.freeClassCount]!;
    this.classFreq[klass] = frequency;
    this.classHead[klass] = NIL;
    this.classTail[klass] = NIL;
    return klass;
  }

  /** Create a class immediately before `before`, or at the end if `before` is NIL. */
  private insertClassBefore(before: number, frequency: number): number {
    const klass = this.allocateClass(frequency);

    if (before === NIL) {
      // The list is empty, so this is the only class.
      this.classPrev[klass] = NIL;
      this.classNext[klass] = NIL;
      this.classListHead = klass;
      return klass;
    }

    const prev = this.classPrev[before]!;
    this.classPrev[klass] = prev;
    this.classNext[klass] = before;
    this.classPrev[before] = klass;
    if (prev !== NIL) this.classNext[prev] = klass;
    else this.classListHead = klass;
    return klass;
  }

  private insertClassAfter(after: number, frequency: number): number {
    const klass = this.allocateClass(frequency);
    const next = this.classNext[after]!;

    this.classPrev[klass] = after;
    this.classNext[klass] = next;
    this.classNext[after] = klass;
    if (next !== NIL) this.classPrev[next] = klass;
    return klass;
  }

  private releaseClass(klass: number): void {
    const next = this.classNext[klass]!;
    const prev = this.classPrev[klass]!;

    if (prev !== NIL) this.classNext[prev] = next;
    else this.classListHead = next;
    if (next !== NIL) this.classPrev[next] = prev;

    this.classNext[klass] = NIL;
    this.classPrev[klass] = NIL;
    this.freeClasses[this.freeClassCount] = klass;
    this.freeClassCount += 1;
  }
}
