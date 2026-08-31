/**
 * S3-FIFO — three FIFO queues, two bits per entry, and no linked-list surgery.
 *
 * The observation behind it is blunt: on web-shaped workloads, most objects are
 * requested exactly once. A cache that admits everything spends most of its
 * capacity on keys it will never serve again. [W-TinyLFU](../w-tinylfu/)
 * answers that with a frequency sketch; S3-FIFO answers it with a **small queue
 * that most objects never leave**.
 *
 * New keys enter **S**, a FIFO holding a tenth of the cache. An object that is
 * requested again while it sits there earns promotion to **M**, the main FIFO;
 * an object that is not simply falls out, leaving its key in **G**, a ghost
 * queue. A key that returns while its ghost is live skips the audition and goes
 * straight into M, because it has now demonstrated reuse across a full pass of
 * the small queue.
 *
 * Inside M, a two-bit counter gives each object up to three second chances, in
 * the manner of [CLOCK](../clock/) — but the queue is still a queue, so there
 * is no list to reorder and nothing to lock on a hit. A hit anywhere is a
 * single counter increment.
 *
 * That is the whole design: no linked-list splicing, no sketch, no adaptive
 * parameter, and hit rates competitive with far more elaborate policies.
 */

export interface S3FifoParams {
  /** Maximum number of entries held. */
  capacity: number;
  /** Fraction of capacity given to the small admission queue. */
  smallFraction: number;
}

const DEFAULT_CAPACITY = 1000;
const DEFAULT_SMALL_FRACTION = 0.1;

/** Queues, used to index the ring state. */
const SMALL = 0;
const MAIN = 1;

/** Two bits per entry: three second chances and no more. */
const MAX_FREQUENCY = 3;
/** An entry must have been requested more than once in S to be promoted. */
const PROMOTION_THRESHOLD = 1;

/** End of the ghost list. */
const GHOST_NIL = -1;

/** Where a key sits. Returned by {@link S3Fifo.queueOf}. */
export type S3FifoQueue = "small" | "main" | "ghost" | "absent";

export default class S3Fifo<K> {
  private readonly capacity: number;
  private readonly smallSize: number;
  private readonly mainSize: number;

  private readonly index: Map<K, number>;
  private readonly keys: (K | undefined)[];
  /** Two-bit access counter per slot. */
  private readonly frequency: Uint8Array;
  /** Which queue a slot is in. */
  private readonly queueOfSlot: Int8Array;

  // Two rings of slot indices. Entries never move within a queue.
  private readonly rings: Int32Array[];
  private readonly ringHeads: Int32Array;
  private readonly ringLengths: Int32Array;

  /**
   * G: keys of objects that fell out of S, as a FIFO list over ghost slots. A
   * list rather than a `Set`, because promotion removes a ghost from the
   * middle and expiry needs the oldest in O(1) — a JS `Set` iterator restarts
   * past every tombstone, which turns steady expire-and-add into O(size).
   */
  private readonly ghostSize: number;
  private readonly ghostKeys: (K | undefined)[];
  private readonly ghostPrev: Int32Array;
  private readonly ghostNext: Int32Array;
  private ghostHead = GHOST_NIL;
  private ghostTail = GHOST_NIL;
  private ghostCount = 0;
  private readonly ghostIndex: Map<K, number>;
  private readonly ghostFree: Int32Array;
  private ghostFreeCount: number;

  private readonly freeSlots: Int32Array;
  private freeCount: number;

  constructor(params: Partial<S3FifoParams> = {}) {
    const capacity = params.capacity ?? DEFAULT_CAPACITY;
    const smallFraction = params.smallFraction ?? DEFAULT_SMALL_FRACTION;

    if (!Number.isInteger(capacity) || capacity < 2) {
      throw new RangeError(
        `S3Fifo: capacity must be an integer of at least 2, received ${capacity}. ` +
          "The small and main queues each need at least one entry.",
      );
    }
    if (!(smallFraction > 0) || smallFraction >= 1) {
      throw new RangeError(
        `S3Fifo: smallFraction must be in (0, 1), received ${smallFraction}`,
      );
    }

    this.capacity = capacity;
    this.smallSize = Math.max(1, Math.floor(capacity * smallFraction));
    this.mainSize = capacity - this.smallSize;

    // One spare: a caller inserts before evicting.
    const slots = capacity + 1;

    this.index = new Map<K, number>();
    this.keys = new Array<K | undefined>(slots);
    this.frequency = new Uint8Array(slots);
    this.queueOfSlot = new Int8Array(slots).fill(-1);

    this.rings = [new Int32Array(slots), new Int32Array(slots)];
    this.ringHeads = new Int32Array(2);
    this.ringLengths = new Int32Array(2);

    // The ghost queue remembers as many keys as the main queue holds entries,
    // so a key gets roughly one main-queue lifetime to prove itself.
    this.ghostSize = this.mainSize;
    this.ghostKeys = new Array<K | undefined>(this.ghostSize);
    this.ghostPrev = new Int32Array(this.ghostSize).fill(GHOST_NIL);
    this.ghostNext = new Int32Array(this.ghostSize).fill(GHOST_NIL);
    this.ghostIndex = new Map<K, number>();
    this.ghostFree = new Int32Array(this.ghostSize);
    for (let slot = 0; slot < this.ghostSize; slot += 1) {
      this.ghostFree[slot] = this.ghostSize - 1 - slot;
    }
    this.ghostFreeCount = this.ghostSize;

    this.freeSlots = new Int32Array(slots);
    for (let slot = 0; slot < slots; slot += 1) this.freeSlots[slot] = slots - 1 - slot;
    this.freeCount = slots;
  }

  onAccess(key: K, hit: boolean): void {
    if (hit) {
      const slot = this.index.get(key);
      if (slot === undefined) {
        throw new Error(`S3Fifo: onAccess reported a hit for a key it does not hold: ${String(key)}`);
      }
      // The entire hit path: bump a two-bit counter. Nothing moves, nothing is
      // reordered, and no other thread needs to see it.
      const current = this.frequency[slot]!;
      if (current < MAX_FREQUENCY) this.frequency[slot] = current + 1;
      return;
    }

    if (this.freeCount === 0) {
      throw new Error(
        `S3Fifo: ${this.capacity + 1} entries inserted without an evict, capacity is ` +
          `${this.capacity}. Call evict() once the cache is over capacity.`,
      );
    }

    this.freeCount -= 1;
    const slot = this.freeSlots[this.freeCount]!;
    this.keys[slot] = key;
    this.frequency[slot] = 0;
    this.index.set(key, slot);

    if (this.ghostIndex.has(key)) {
      // The key fell out of S once and came back, which is the reuse the small
      // queue exists to detect. It skips the audition entirely.
      this.forgetGhost(key);
      this.push(MAIN, slot);
      return;
    }
    this.push(SMALL, slot);
  }

  evict(): K {
    // The small queue is drained while it is over its share. Only once it is
    // back within its share does the main queue give anything up, so an object
    // that has not proven reuse is always the better victim.
    if (this.ringLengths[SMALL]! >= this.smallSize) {
      const evicted = this.evictFromSmall();
      if (evicted !== undefined) return evicted;
    }
    return this.evictFromMain();
  }

  /** Entries currently held. Not part of the interface; used by tests. */
  size(): number {
    return this.index.size;
  }

  /** Where a key lives right now. Not part of the interface; used by tests. */
  queueOf(key: K): S3FifoQueue {
    const slot = this.index.get(key);
    if (slot !== undefined) return this.queueOfSlot[slot] === SMALL ? "small" : "main";
    return this.ghostIndex.has(key) ? "ghost" : "absent";
  }

  /** The two-bit counter for a key, or 0. Not part of the interface; used by tests. */
  frequencyOf(key: K): number {
    const slot = this.index.get(key);
    return slot === undefined ? 0 : this.frequency[slot]!;
  }

  /**
   * Drain the small queue until something leaves the cache.
   *
   * An object requested more than once while in S moves to M and keeps its
   * counter. The first object that has not been is evicted, and its key is
   * remembered in the ghost queue. Returns undefined if S emptied without
   * evicting anything, in which case the main queue is asked instead.
   */
  private evictFromSmall(): K | undefined {
    while (this.ringLengths[SMALL]! > 0) {
      const slot = this.pop(SMALL);
      if (this.frequency[slot]! > PROMOTION_THRESHOLD) {
        this.push(MAIN, slot);
        continue;
      }

      const key = this.keys[slot] as K;
      this.rememberGhost(key);
      this.release(slot, key);
      return key;
    }
    return undefined;
  }

  /**
   * Take from the main queue, giving each object its remaining second chances.
   *
   * An object with a non-zero counter is reinserted at the head with the
   * counter decremented, exactly as CLOCK's hand clears a reference bit. This
   * terminates because every pass spends a chance.
   */
  private evictFromMain(): K {
    while (this.ringLengths[MAIN]! > 0) {
      const slot = this.pop(MAIN);
      const current = this.frequency[slot]!;
      if (current > 0) {
        this.frequency[slot] = current - 1;
        this.push(MAIN, slot);
        continue;
      }

      const key = this.keys[slot] as K;
      this.release(slot, key);
      return key;
    }

    // The main queue is empty; fall back to the small one however short it is.
    const fallback = this.evictFromSmall();
    if (fallback !== undefined) return fallback;
    throw new Error("S3Fifo: evict() called with nothing resident");
  }

  // --- rings ----------------------------------------------------------------

  private push(queue: number, slot: number): void {
    const ring = this.rings[queue]!;
    let index = this.ringHeads[queue]! + this.ringLengths[queue]!;
    if (index >= ring.length) index -= ring.length;
    ring[index] = slot;
    this.ringLengths[queue] = this.ringLengths[queue]! + 1;
    this.queueOfSlot[slot] = queue;
  }

  private pop(queue: number): number {
    const ring = this.rings[queue]!;
    const head = this.ringHeads[queue]!;
    const slot = ring[head]!;
    let next = head + 1;
    if (next >= ring.length) next = 0;
    this.ringHeads[queue] = next;
    this.ringLengths[queue] = this.ringLengths[queue]! - 1;
    return slot;
  }

  private release(slot: number, key: K): void {
    this.index.delete(key);
    this.keys[slot] = undefined;
    this.frequency[slot] = 0;
    this.queueOfSlot[slot] = -1;
    this.freeSlots[this.freeCount] = slot;
    this.freeCount += 1;
  }

  // --- ghosts ---------------------------------------------------------------

  private rememberGhost(key: K): void {
    let slot: number;
    if (this.ghostCount === this.ghostSize) {
      // Full, so the oldest identifier is forgotten.
      slot = this.ghostTail;
      this.ghostIndex.delete(this.ghostKeys[slot] as K);
      this.unlinkGhost(slot);
    } else {
      this.ghostFreeCount -= 1;
      slot = this.ghostFree[this.ghostFreeCount]!;
    }
    this.ghostKeys[slot] = key;
    this.linkGhostHead(slot);
    this.ghostIndex.set(key, slot);
  }

  /**
   * Promotion removes exactly this ghost; the others keep their order and
   * their claim on G's capacity.
   */
  private forgetGhost(key: K): void {
    const slot = this.ghostIndex.get(key)!;
    this.ghostIndex.delete(key);
    this.ghostKeys[slot] = undefined;
    this.unlinkGhost(slot);
    this.ghostFree[this.ghostFreeCount] = slot;
    this.ghostFreeCount += 1;
  }

  private linkGhostHead(slot: number): void {
    this.ghostPrev[slot] = GHOST_NIL;
    this.ghostNext[slot] = this.ghostHead;
    if (this.ghostHead !== GHOST_NIL) this.ghostPrev[this.ghostHead] = slot;
    else this.ghostTail = slot;
    this.ghostHead = slot;
    this.ghostCount += 1;
  }

  private unlinkGhost(slot: number): void {
    const next = this.ghostNext[slot]!;
    const prev = this.ghostPrev[slot]!;
    if (prev !== GHOST_NIL) this.ghostNext[prev] = next;
    else this.ghostHead = next;
    if (next !== GHOST_NIL) this.ghostPrev[next] = prev;
    else this.ghostTail = prev;
    this.ghostNext[slot] = GHOST_NIL;
    this.ghostPrev[slot] = GHOST_NIL;
    this.ghostCount -= 1;
  }
}
