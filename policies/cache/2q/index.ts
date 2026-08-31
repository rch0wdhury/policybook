/**
 * 2Q — admit to the main cache only on the *second* access.
 *
 * LRU treats every miss as evidence that a key is worth keeping. Most of the
 * time it is wrong: a key seen once and never again still displaces something
 * useful. 2Q's answer is to make keys audition.
 *
 * A new key goes into **A1in**, a small FIFO holding roughly a quarter of the
 * cache. Keys evicted from A1in are not forgotten entirely — their identifiers
 * go to **A1out**, a ghost queue that stores keys but no values, so it costs
 * almost nothing. A key that comes back while its ghost is still around has
 * proven reuse, and only then is it promoted to **Am**, the main LRU.
 *
 * The consequence is that a scan cannot reach Am at all. Its keys arrive once,
 * pass through A1in, and leave; they never touch the working set. That is the
 * same problem [SIEVE](../sieve/) and [S3-FIFO](../s3-fifo/) solve more
 * cheaply, but 2Q got there in 1994 and remains the clearest illustration of
 * the idea.
 *
 * One detail is easy to get wrong: a hit on a key still in A1in does
 * **nothing**. It is not promoted and not reordered. Promotion happens only
 * through A1out, which is what makes the test a genuine second *access*, not
 * merely a second reference in quick succession.
 */

export interface TwoQueueParams {
  /** Maximum number of entries held. */
  capacity: number;
  /** Fraction of capacity given to the A1in admission queue. */
  kin: number;
  /** Fraction of capacity worth of keys remembered in the A1out ghost queue. */
  kout: number;
}

const DEFAULT_CAPACITY = 1000;
const DEFAULT_KIN = 0.25;
const DEFAULT_KOUT = 0.5;
const NIL = -1;

/** Which queue an entry is in. Returned by {@link TwoQueue.queueOf}. */
export type TwoQueueLocation = "a1in" | "am" | "ghost" | "absent";

export default class TwoQueue<K> {
  private readonly capacity: number;
  /** Entries A1in may hold before evictions are taken from it. */
  private readonly kinSize: number;
  /** Keys A1out remembers. */
  private readonly koutSize: number;

  private readonly index: Map<K, number>;
  private readonly keys: (K | undefined)[];
  /** 1 if the slot is in Am, 0 if it is in A1in. */
  private readonly inMain: Uint8Array;

  /** A1in: a FIFO ring of slots. Entries leave only from its head. */
  private readonly admission: Int32Array;
  private admissionHead = 0;
  private admissionLength = 0;

  /** Am: an LRU list over slots. */
  private readonly next: Int32Array;
  private readonly prev: Int32Array;
  private mainHead = NIL;
  private mainTail = NIL;
  private mainLength = 0;

  /**
   * A1out: keys with no values behind them, as a FIFO list over ghost slots. A
   * list rather than a `Set`, because promotion removes a ghost from the
   * middle and expiry needs the oldest in O(1) — a JS `Set` iterator restarts
   * past every tombstone, which turns steady expire-and-add into O(size).
   */
  private readonly ghostKeys: (K | undefined)[];
  private readonly ghostPrev: Int32Array;
  private readonly ghostNext: Int32Array;
  private ghostHead = NIL;
  private ghostTail = NIL;
  private ghostCount = 0;
  private readonly ghostIndex: Map<K, number>;
  private readonly ghostFree: Int32Array;
  private ghostFreeCount: number;

  private readonly freeSlots: Int32Array;
  private freeCount: number;

  constructor(params: Partial<TwoQueueParams> = {}) {
    const capacity = params.capacity ?? DEFAULT_CAPACITY;
    const kin = params.kin ?? DEFAULT_KIN;
    const kout = params.kout ?? DEFAULT_KOUT;

    if (!Number.isInteger(capacity) || capacity < 1) {
      throw new RangeError(`TwoQueue: capacity must be a positive integer, received ${capacity}`);
    }
    if (!(kin > 0) || kin > 1) {
      throw new RangeError(`TwoQueue: kin must be a fraction in (0, 1], received ${kin}`);
    }
    if (!(kout > 0) || kout > 1) {
      throw new RangeError(`TwoQueue: kout must be a fraction in (0, 1], received ${kout}`);
    }

    this.capacity = capacity;
    // Both fractions floor to at least one entry, so a small cache still has a
    // working admission queue rather than silently degenerating to LRU.
    this.kinSize = Math.max(1, Math.floor(capacity * kin));
    this.koutSize = Math.max(1, Math.floor(capacity * kout));

    // One spare: a caller inserts before evicting.
    const slots = capacity + 1;

    this.index = new Map<K, number>();
    this.keys = new Array<K | undefined>(slots);
    this.inMain = new Uint8Array(slots);
    this.admission = new Int32Array(slots);
    this.next = new Int32Array(slots).fill(NIL);
    this.prev = new Int32Array(slots).fill(NIL);
    this.ghostKeys = new Array<K | undefined>(this.koutSize);
    this.ghostPrev = new Int32Array(this.koutSize).fill(NIL);
    this.ghostNext = new Int32Array(this.koutSize).fill(NIL);
    this.ghostIndex = new Map<K, number>();
    this.ghostFree = new Int32Array(this.koutSize);
    for (let slot = 0; slot < this.koutSize; slot += 1) {
      this.ghostFree[slot] = this.koutSize - 1 - slot;
    }
    this.ghostFreeCount = this.koutSize;
    this.freeSlots = new Int32Array(slots);
    for (let slot = 0; slot < slots; slot += 1) this.freeSlots[slot] = slots - 1 - slot;
    this.freeCount = slots;
  }

  onAccess(key: K, hit: boolean): void {
    if (hit) {
      const slot = this.index.get(key);
      if (slot === undefined) {
        throw new Error(
          `TwoQueue: onAccess reported a hit for a key it does not hold: ${String(key)}`,
        );
      }
      // A hit in Am refreshes recency. A hit in A1in does nothing at all: the
      // key has not yet earned promotion, and reordering A1in would make it a
      // second LRU rather than an audition.
      if (this.inMain[slot] === 1) this.moveToMainHead(slot);
      return;
    }

    if (this.freeCount === 0) {
      throw new Error(
        `TwoQueue: ${this.capacity + 1} entries inserted without an evict, capacity is ${this.capacity}. ` +
          "Call evict() once the cache is over capacity.",
      );
    }

    this.freeCount -= 1;
    const slot = this.freeSlots[this.freeCount]!;
    this.keys[slot] = key;
    this.index.set(key, slot);

    if (this.ghostIndex.has(key)) {
      // Seen before and back again: this is the second access the policy is
      // waiting for, so the key goes straight into the main cache.
      this.forgetGhost(key);
      this.inMain[slot] = 1;
      this.linkMainHead(slot);
      return;
    }

    this.inMain[slot] = 0;
    this.pushAdmission(slot);
  }

  evict(): K {
    // Drain A1in while it is over its share: an entry that has not proven
    // reuse is always a better victim than one that has.
    const takeFromAdmission =
      this.admissionLength > 0 && (this.admissionLength > this.kinSize || this.mainLength === 0);

    if (takeFromAdmission) {
      const slot = this.popAdmission();
      const key = this.keys[slot] as K;
      // Remember the key so a prompt return promotes it. Only A1in evictions
      // leave a ghost; an Am entry has already proven itself once.
      this.rememberGhost(key);
      this.release(slot, key);
      return key;
    }

    if (this.mainLength === 0) {
      throw new Error("TwoQueue: evict() called with nothing resident");
    }

    const slot = this.mainTail;
    const key = this.keys[slot] as K;
    this.unlinkMain(slot);
    this.release(slot, key);
    return key;
  }

  /** Entries currently held. Not part of the interface; used by tests. */
  size(): number {
    return this.index.size;
  }

  /** Where a key lives right now. Not part of the interface; used by tests. */
  queueOf(key: K): TwoQueueLocation {
    const slot = this.index.get(key);
    if (slot !== undefined) return this.inMain[slot] === 1 ? "am" : "a1in";
    return this.ghostIndex.has(key) ? "ghost" : "absent";
  }

  private release(slot: number, key: K): void {
    this.index.delete(key);
    this.keys[slot] = undefined;
    this.inMain[slot] = 0;
    this.freeSlots[this.freeCount] = slot;
    this.freeCount += 1;
  }

  // --- A1in ------------------------------------------------------------------

  private pushAdmission(slot: number): void {
    let index = this.admissionHead + this.admissionLength;
    if (index >= this.admission.length) index -= this.admission.length;
    this.admission[index] = slot;
    this.admissionLength += 1;
  }

  private popAdmission(): number {
    const slot = this.admission[this.admissionHead]!;
    this.admissionHead += 1;
    if (this.admissionHead >= this.admission.length) this.admissionHead = 0;
    this.admissionLength -= 1;
    return slot;
  }

  // --- A1out -----------------------------------------------------------------

  private rememberGhost(key: K): void {
    let slot: number;
    if (this.ghostCount === this.koutSize) {
      // The ghost queue is full, so the oldest identifier is forgotten. A key
      // whose ghost has expired has to start from A1in again.
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
   * their claim on A1out's capacity.
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
    this.ghostPrev[slot] = NIL;
    this.ghostNext[slot] = this.ghostHead;
    if (this.ghostHead !== NIL) this.ghostPrev[this.ghostHead] = slot;
    else this.ghostTail = slot;
    this.ghostHead = slot;
    this.ghostCount += 1;
  }

  private unlinkGhost(slot: number): void {
    const next = this.ghostNext[slot]!;
    const prev = this.ghostPrev[slot]!;
    if (prev !== NIL) this.ghostNext[prev] = next;
    else this.ghostHead = next;
    if (next !== NIL) this.ghostPrev[next] = prev;
    else this.ghostTail = prev;
    this.ghostNext[slot] = NIL;
    this.ghostPrev[slot] = NIL;
    this.ghostCount -= 1;
  }

  // --- Am --------------------------------------------------------------------

  private linkMainHead(slot: number): void {
    this.prev[slot] = NIL;
    this.next[slot] = this.mainHead;
    if (this.mainHead !== NIL) this.prev[this.mainHead] = slot;
    else this.mainTail = slot;
    this.mainHead = slot;
    this.mainLength += 1;
  }

  private unlinkMain(slot: number): void {
    const next = this.next[slot]!;
    const prev = this.prev[slot]!;
    if (prev !== NIL) this.next[prev] = next;
    else this.mainHead = next;
    if (next !== NIL) this.prev[next] = prev;
    else this.mainTail = prev;
    this.next[slot] = NIL;
    this.prev[slot] = NIL;
    this.mainLength -= 1;
  }

  private moveToMainHead(slot: number): void {
    if (this.mainHead === slot) return;
    this.unlinkMain(slot);
    this.linkMainHead(slot);
  }
}
