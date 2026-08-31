/**
 * SIEVE — simpler than LRU, and better on web workloads.
 *
 * SIEVE looks like CLOCK at first glance: one bit per entry, set on a hit, and
 * a hand that sweeps clearing bits. The difference is what happens to a
 * survivor. CLOCK moves it to the back of the queue, granting it a full new
 * lifetime. SIEVE **leaves it exactly where it is** and keeps the hand where it
 * stopped.
 *
 * That one change is the whole idea, and it has two consequences. Old objects
 * that keep being used stay near the old end and are re-examined often, so they
 * must keep earning their place — "lazy promotion". And new objects are
 * inserted at the new end, ahead of the hand, so they face eviction before
 * they have travelled the whole queue — "quick demotion". One-hit wonders die
 * young, which is what a web cache spends most of its capacity on.
 *
 * The result is a policy with no lock on the hit path, no reordering at all,
 * and a higher hit rate than LRU on the traces its authors measured.
 */

export interface SieveParams {
  /** Maximum number of entries held. */
  capacity: number;
}

const DEFAULT_CAPACITY = 1000;
const NIL = -1;

export default class Sieve<K> {
  private readonly capacity: number;

  private readonly index: Map<K, number>;
  private readonly keys: (K | undefined)[];
  /** One bit per slot: set on a hit, cleared by the hand. */
  private readonly visited: Uint8Array;

  // Insertion order. Nothing ever moves within it.
  private readonly newer: Int32Array;
  private readonly older: Int32Array;
  private newest = NIL;
  private oldest = NIL;

  /**
   * Where the hand stopped last time, or NIL to start from the oldest entry.
   *
   * Retaining this across evictions is the point: it is what makes the sweep
   * continuous rather than restarting at the oldest entry every time.
   */
  private hand = NIL;

  private readonly freeSlots: Int32Array;
  private freeCount: number;

  constructor(params: Partial<SieveParams> = {}) {
    const capacity = params.capacity ?? DEFAULT_CAPACITY;
    if (!Number.isInteger(capacity) || capacity < 1) {
      throw new RangeError(`Sieve: capacity must be a positive integer, received ${capacity}`);
    }

    this.capacity = capacity;
    // One spare: a caller inserts before evicting.
    const slots = capacity + 1;

    this.index = new Map<K, number>();
    this.keys = new Array<K | undefined>(slots);
    this.visited = new Uint8Array(slots);
    this.newer = new Int32Array(slots).fill(NIL);
    this.older = new Int32Array(slots).fill(NIL);
    this.freeSlots = new Int32Array(slots);
    for (let slot = 0; slot < slots; slot += 1) this.freeSlots[slot] = slots - 1 - slot;
    this.freeCount = slots;
  }

  onAccess(key: K, hit: boolean): void {
    if (hit) {
      const slot = this.index.get(key);
      if (slot === undefined) {
        throw new Error(`Sieve: onAccess reported a hit for a key it does not hold: ${String(key)}`);
      }
      // The entire hit path: set one bit. The entry does not move.
      this.visited[slot] = 1;
      return;
    }

    if (this.freeCount === 0) {
      throw new Error(
        `Sieve: ${this.capacity + 1} entries inserted without an evict, capacity is ${this.capacity}. ` +
          "Call evict() once the cache is over capacity.",
      );
    }

    this.freeCount -= 1;
    const slot = this.freeSlots[this.freeCount]!;
    this.keys[slot] = key;
    this.visited[slot] = 0;
    this.index.set(key, slot);

    // New entries go at the new end, ahead of the hand.
    this.newer[slot] = NIL;
    this.older[slot] = this.newest;
    if (this.newest !== NIL) this.newer[this.newest] = slot;
    else this.oldest = slot;
    this.newest = slot;
  }

  evict(): K {
    if (this.oldest === NIL) {
      throw new Error("Sieve: evict() called with nothing resident");
    }

    // Resume where the hand stopped, or start at the oldest entry.
    let slot = this.hand === NIL ? this.oldest : this.hand;

    while (this.visited[slot] === 1) {
      this.visited[slot] = 0;
      const next = this.newer[slot]!;
      // Past the newest entry, the hand wraps to the oldest.
      slot = next === NIL ? this.oldest : next;
    }

    // The hand stops just beyond the victim, and stays there.
    this.hand = this.newer[slot]!;

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

  /** Whether a key's visited bit is set. Not part of the interface; used by tests. */
  isVisited(key: K): boolean {
    const slot = this.index.get(key);
    return slot !== undefined && this.visited[slot] === 1;
  }

  private unlink(slot: number): void {
    const newer = this.newer[slot]!;
    const older = this.older[slot]!;

    if (newer !== NIL) this.older[newer] = older;
    else this.newest = older;
    if (older !== NIL) this.newer[older] = newer;
    else this.oldest = newer;

    this.newer[slot] = NIL;
    this.older[slot] = NIL;
  }
}
