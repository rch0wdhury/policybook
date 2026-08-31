/**
 * Bélády's OPT — evict whatever will be needed furthest in the future.
 *
 * This is not a policy you can deploy. It requires the entire future access
 * sequence, which no real cache has. It is here because it is the *bound*: no
 * online policy can beat it on the same trace, so the gap between a policy's
 * hit rate and OPT's is the honest measure of how much is left on the table.
 *
 * Bélády proved in 1966 that "furthest next use" is optimal, and the proof is
 * the reason this entry exists rather than a table of numbers: when you see
 * [SIEVE](../sieve/) reach 0.73 and OPT reach 0.81 on the same trace, you know
 * precisely what the remaining 0.08 is worth, and that nothing will ever
 * recover more.
 *
 * The implementation precomputes, for every position in the trace, where that
 * key is next used. Eviction is then a maximum over resident entries, kept in
 * an indexed binary heap so it costs O(log n) rather than a scan.
 */

export interface OptParams<K> {
  /** Maximum number of entries held. */
  capacity: number;
  /**
   * The complete access sequence, in order.
   *
   * The policy consumes one element per `onAccess` call and checks that the key
   * matches; a mismatch means the caller is replaying a different trace than
   * the one OPT was given, which would silently produce a bound that is not a
   * bound.
   */
  future: K[];
}

const DEFAULT_CAPACITY = 1000;
const NIL = -1;

export default class Opt<K> {
  private readonly capacity: number;
  private readonly future: K[];

  /**
   * For each position in the trace, the next position holding the same key, or
   * the trace length if there is none.
   *
   * Using the length as "never again" keeps every comparison an integer one:
   * it is strictly greater than any real position, so no infinity is needed.
   */
  private readonly nextUseAt: Int32Array;
  /** How many accesses have been consumed. */
  private step = 0;

  private readonly index: Map<K, number>;
  private readonly keys: (K | undefined)[];
  /** The next position at which each resident slot's key is used. */
  private readonly nextUse: Int32Array;
  /** Order of insertion, used only to break ties deterministically. */
  private readonly insertedAt: Int32Array;
  private insertCounter = 0;

  // A binary max-heap of slots, ordered by next use. `heapPos` makes it
  // indexed, so a slot's priority can be updated in place when its key is
  // accessed again rather than leaving a stale entry behind.
  private readonly heap: Int32Array;
  private readonly heapPos: Int32Array;
  private heapSize = 0;

  private readonly freeSlots: Int32Array;
  private freeCount: number;

  constructor(params: Partial<OptParams<K>> = {}) {
    const capacity = params.capacity ?? DEFAULT_CAPACITY;
    const future = params.future ?? [];

    if (!Number.isInteger(capacity) || capacity < 1) {
      throw new RangeError(`Opt: capacity must be a positive integer, received ${capacity}`);
    }
    if (!Array.isArray(future)) {
      throw new TypeError("Opt: future must be an array holding the complete access sequence");
    }

    this.capacity = capacity;
    this.future = future;

    // One backward pass records where each key is next used.
    this.nextUseAt = new Int32Array(future.length);
    const lastSeen = new Map<K, number>();
    for (let position = future.length - 1; position >= 0; position -= 1) {
      const key = future[position] as K;
      this.nextUseAt[position] = lastSeen.get(key) ?? future.length;
      lastSeen.set(key, position);
    }

    const slots = capacity + 1;
    this.index = new Map<K, number>();
    this.keys = new Array<K | undefined>(slots);
    this.nextUse = new Int32Array(slots);
    this.insertedAt = new Int32Array(slots);
    this.heap = new Int32Array(slots);
    this.heapPos = new Int32Array(slots).fill(NIL);
    this.freeSlots = new Int32Array(slots);
    for (let slot = 0; slot < slots; slot += 1) this.freeSlots[slot] = slots - 1 - slot;
    this.freeCount = slots;
  }

  onAccess(key: K, hit: boolean): void {
    if (this.step >= this.future.length) {
      throw new Error(
        `Opt: access beyond the end of the supplied future (${this.future.length} events). ` +
          "OPT must be given the whole trace it will be run on.",
      );
    }
    if (this.future[this.step] !== key) {
      throw new Error(
        `Opt: the trace does not match the supplied future at event ${this.step}: ` +
          `expected ${String(this.future[this.step])}, got ${String(key)}. ` +
          "OPT's result is only a bound for the trace it was given.",
      );
    }

    const nextUse = this.nextUseAt[this.step]!;
    this.step += 1;

    if (hit) {
      const slot = this.index.get(key);
      if (slot === undefined) {
        throw new Error(`Opt: onAccess reported a hit for a key it does not hold: ${String(key)}`);
      }
      // The key's next use has moved further out; its place in the heap moves
      // with it rather than leaving a stale entry to skip over later.
      this.nextUse[slot] = nextUse;
      this.reheapify(slot);
      return;
    }

    if (this.freeCount === 0) {
      throw new Error(
        `Opt: ${this.capacity + 1} entries inserted without an evict, capacity is ${this.capacity}. ` +
          "Call evict() once the cache is over capacity.",
      );
    }

    this.freeCount -= 1;
    const slot = this.freeSlots[this.freeCount]!;
    this.keys[slot] = key;
    this.nextUse[slot] = nextUse;
    this.insertedAt[slot] = this.insertCounter;
    this.insertCounter += 1;
    this.index.set(key, slot);
    this.heapInsert(slot);
  }

  evict(): K {
    if (this.heapSize === 0) {
      throw new Error("Opt: evict() called with nothing resident");
    }

    const slot = this.heap[0]!;
    const key = this.keys[slot] as K;

    this.heapRemoveRoot();
    this.index.delete(key);
    this.keys[slot] = undefined;
    this.freeSlots[this.freeCount] = slot;
    this.freeCount += 1;
    return key;
  }

  /** Entries currently held. Not part of the interface; used by tests. */
  size(): number {
    return this.index.size;
  }

  /**
   * The next position at which a resident key is used, or the trace length if
   * it is never used again. Not part of the interface; used by tests.
   */
  nextUseOf(key: K): number {
    const slot = this.index.get(key);
    return slot === undefined ? -1 : this.nextUse[slot]!;
  }

  // --- the indexed max-heap --------------------------------------------------

  /**
   * True when `a` should sit closer to the root than `b`.
   *
   * Furthest next use wins. Among keys never used again — which all share the
   * same sentinel — the earliest inserted goes first, so the choice is fully
   * determined rather than left to heap order.
   */
  private outranks(a: number, b: number): boolean {
    const useA = this.nextUse[a]!;
    const useB = this.nextUse[b]!;
    if (useA !== useB) return useA > useB;
    return this.insertedAt[a]! < this.insertedAt[b]!;
  }

  private swap(i: number, j: number): void {
    const slotI = this.heap[i]!;
    const slotJ = this.heap[j]!;
    this.heap[i] = slotJ;
    this.heap[j] = slotI;
    this.heapPos[slotJ] = i;
    this.heapPos[slotI] = j;
  }

  private siftUp(start: number): void {
    let index = start;
    while (index > 0) {
      const parent = (index - 1) >> 1;
      if (!this.outranks(this.heap[index]!, this.heap[parent]!)) break;
      this.swap(index, parent);
      index = parent;
    }
  }

  private siftDown(start: number): void {
    let index = start;
    for (;;) {
      const left = index * 2 + 1;
      if (left >= this.heapSize) break;
      const right = left + 1;
      let best = left;
      if (right < this.heapSize && this.outranks(this.heap[right]!, this.heap[left]!)) best = right;
      if (!this.outranks(this.heap[best]!, this.heap[index]!)) break;
      this.swap(index, best);
      index = best;
    }
  }

  private heapInsert(slot: number): void {
    this.heap[this.heapSize] = slot;
    this.heapPos[slot] = this.heapSize;
    this.heapSize += 1;
    this.siftUp(this.heapSize - 1);
  }

  private reheapify(slot: number): void {
    const position = this.heapPos[slot]!;
    this.siftDown(position);
    this.siftUp(this.heapPos[slot]!);
  }

  private heapRemoveRoot(): void {
    const root = this.heap[0]!;
    this.heapPos[root] = NIL;
    this.heapSize -= 1;

    if (this.heapSize > 0) {
      const moved = this.heap[this.heapSize]!;
      this.heap[0] = moved;
      this.heapPos[moved] = 0;
      this.siftDown(0);
    }
  }
}
