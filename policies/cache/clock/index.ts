/**
 * CLOCK — LRU's hit rate without LRU's write on every hit.
 *
 * Each entry carries one reference bit. A hit sets the bit and touches nothing
 * else — no list to reorder, no lock to take. Eviction walks a hand around the
 * entries in arrival order: an entry whose bit is set gets a second chance, its
 * bit cleared, and the hand moves on; the first entry with a clear bit is
 * evicted.
 *
 * The result approximates LRU closely enough for most workloads while making
 * reads pure. That is why operating systems have used it since Multics and why
 * it remains the default answer for a cache that is read from many threads.
 *
 * Implemented here as the queue formulation — usually called second-chance —
 * which is the same algorithm as the circular buffer with a hand. Re-examining
 * an entry and pushing it back is exactly what advancing a hand past it does;
 * the queue's front *is* the hand. The queue form is easier to read and easier
 * to reproduce identically in three languages.
 */

export interface ClockParams {
  /** Maximum number of entries held. */
  capacity: number;
}

const DEFAULT_CAPACITY = 1000;

export default class Clock<K> {
  private readonly capacity: number;

  private readonly index: Map<K, number>;
  private readonly keys: (K | undefined)[];
  /** One reference bit per slot: set on a hit, cleared by the hand. */
  private readonly referenced: Uint8Array;

  /** Slot indices in arrival order; the front is the hand. */
  private readonly order: Int32Array;
  private orderHead = 0;
  private orderLength = 0;

  private readonly freeSlots: Int32Array;
  private freeCount: number;

  constructor(params: Partial<ClockParams> = {}) {
    const capacity = params.capacity ?? DEFAULT_CAPACITY;
    if (!Number.isInteger(capacity) || capacity < 1) {
      throw new RangeError(`Clock: capacity must be a positive integer, received ${capacity}`);
    }

    this.capacity = capacity;
    // One spare: a caller inserts before evicting.
    const slots = capacity + 1;

    this.index = new Map<K, number>();
    this.keys = new Array<K | undefined>(slots);
    this.referenced = new Uint8Array(slots);
    this.order = new Int32Array(slots);
    this.freeSlots = new Int32Array(slots);
    for (let slot = 0; slot < slots; slot += 1) this.freeSlots[slot] = slots - 1 - slot;
    this.freeCount = slots;
  }

  onAccess(key: K, hit: boolean): void {
    if (hit) {
      const slot = this.index.get(key);
      if (slot === undefined) {
        throw new Error(`Clock: onAccess reported a hit for a key it does not hold: ${String(key)}`);
      }
      // The entire hit path: set one bit. No reordering, no shared writes.
      this.referenced[slot] = 1;
      return;
    }

    if (this.freeCount === 0) {
      throw new Error(
        `Clock: ${this.capacity + 1} entries inserted without an evict, capacity is ${this.capacity}. ` +
          "Call evict() once the cache is over capacity.",
      );
    }

    this.freeCount -= 1;
    const slot = this.freeSlots[this.freeCount]!;
    this.keys[slot] = key;
    this.referenced[slot] = 0;
    this.index.set(key, slot);
    this.pushBack(slot);
  }

  evict(): K {
    if (this.orderLength === 0) {
      throw new Error("Clock: evict() called with nothing resident");
    }

    // The hand sweeps until it finds an entry without a second chance. It can
    // pass every entry at most once, because passing one clears its bit.
    for (;;) {
      const slot = this.popFront();
      if (this.referenced[slot] === 1) {
        this.referenced[slot] = 0;
        this.pushBack(slot);
        continue;
      }

      const key = this.keys[slot] as K;
      this.index.delete(key);
      this.keys[slot] = undefined;
      this.freeSlots[this.freeCount] = slot;
      this.freeCount += 1;
      return key;
    }
  }

  /** Entries currently held. Not part of the interface; used by tests. */
  size(): number {
    return this.index.size;
  }

  /** Whether a key's reference bit is set. Not part of the interface; used by tests. */
  isReferenced(key: K): boolean {
    const slot = this.index.get(key);
    return slot !== undefined && this.referenced[slot] === 1;
  }

  private pushBack(slot: number): void {
    let index = this.orderHead + this.orderLength;
    if (index >= this.order.length) index -= this.order.length;
    this.order[index] = slot;
    this.orderLength += 1;
  }

  private popFront(): number {
    const slot = this.order[this.orderHead]!;
    this.orderHead += 1;
    if (this.orderHead >= this.order.length) this.orderHead = 0;
    this.orderLength -= 1;
    return slot;
  }
}
