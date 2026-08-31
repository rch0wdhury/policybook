/**
 * LRU — evict the key used longest ago.
 *
 * The default baseline, and the policy most people mean when they say "cache".
 * It assumes recency predicts reuse, which is true often enough that LRU is
 * hard to beat by much and easy to beat by a little.
 *
 * The implementation is the usual one: a hash map from key to slot, and a
 * doubly linked list over those slots giving recency order. The links live in
 * two `Int32Array`s rather than in objects, so a hit is a few array writes and
 * allocates nothing.
 */

export interface LruParams {
  /** Maximum number of entries held. */
  capacity: number;
}

const DEFAULT_CAPACITY = 1000;
const NIL = -1;

export default class Lru<K> {
  private readonly capacity: number;

  /** key -> slot index. */
  private readonly index: Map<K, number>;
  /** slot index -> key, for eviction. */
  private readonly keys: (K | undefined)[];

  // Recency order. head is most recently used, tail is the eviction candidate.
  private readonly next: Int32Array;
  private readonly prev: Int32Array;
  private head = NIL;
  private tail = NIL;

  /** Stack of unused slot indices. */
  private readonly freeSlots: Int32Array;
  private freeCount: number;

  constructor(params: Partial<LruParams> = {}) {
    const capacity = params.capacity ?? DEFAULT_CAPACITY;
    if (!Number.isInteger(capacity) || capacity < 1) {
      throw new RangeError(`Lru: capacity must be a positive integer, received ${capacity}`);
    }

    this.capacity = capacity;
    // One spare slot: a caller inserts before evicting, so the list is briefly
    // one over capacity.
    const slots = capacity + 1;

    this.index = new Map<K, number>();
    this.keys = new Array<K | undefined>(slots);
    this.next = new Int32Array(slots).fill(NIL);
    this.prev = new Int32Array(slots).fill(NIL);

    this.freeSlots = new Int32Array(slots);
    for (let slot = 0; slot < slots; slot += 1) this.freeSlots[slot] = slots - 1 - slot;
    this.freeCount = slots;
  }

  onAccess(key: K, hit: boolean): void {
    if (hit) {
      const slot = this.index.get(key);
      if (slot === undefined) {
        throw new Error(`Lru: onAccess reported a hit for a key it does not hold: ${String(key)}`);
      }
      this.moveToFront(slot);
      return;
    }

    if (this.freeCount === 0) {
      throw new Error(
        `Lru: ${this.capacity + 1} entries inserted without an evict, capacity is ${this.capacity}. ` +
          "Call evict() once the cache is over capacity.",
      );
    }

    this.freeCount -= 1;
    const slot = this.freeSlots[this.freeCount]!;
    this.keys[slot] = key;
    this.index.set(key, slot);
    this.linkFront(slot);
  }

  evict(): K {
    if (this.tail === NIL) {
      throw new Error("Lru: evict() called with nothing resident");
    }

    const slot = this.tail;
    const key = this.keys[slot] as K;

    this.unlink(slot);
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

  private linkFront(slot: number): void {
    this.prev[slot] = NIL;
    this.next[slot] = this.head;
    if (this.head !== NIL) this.prev[this.head] = slot;
    else this.tail = slot;
    this.head = slot;
  }

  private unlink(slot: number): void {
    const next = this.next[slot]!;
    const prev = this.prev[slot]!;
    if (prev !== NIL) this.next[prev] = next;
    else this.head = next;
    if (next !== NIL) this.prev[next] = prev;
    else this.tail = prev;
    this.next[slot] = NIL;
    this.prev[slot] = NIL;
  }

  private moveToFront(slot: number): void {
    if (this.head === slot) return;
    this.unlink(slot);
    this.linkFront(slot);
  }
}
