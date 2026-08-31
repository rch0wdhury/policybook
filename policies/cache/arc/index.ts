/**
 * ARC — balance recency against frequency, and tune the balance itself.
 *
 * [2Q](../2q/) splits the cache between keys seen once and keys seen twice, and
 * makes you choose the split. ARC makes the same split and then *learns* where
 * it should be.
 *
 * Four lists. **T1** holds keys seen once recently, **T2** keys seen at least
 * twice. Behind each sits a ghost list of keys evicted from it — **B1** and
 * **B2** — holding identifiers and no values. A target size `p` says how much of
 * the cache T1 should get.
 *
 * The adaptation is the whole idea, and it is beautifully direct. A hit in B1
 * means "I evicted a recent key too soon, I should keep more recent keys", so
 * `p` grows. A hit in B2 means "I evicted a frequent key too soon", so `p`
 * shrinks. The ghosts are exactly the evidence needed to know which mistake was
 * just made, and each one costs a key rather than a value.
 *
 * The result adapts to a workload changing under it, without tuning, which is
 * why ARC is the standard against which adaptive policies are measured.
 *
 * Implemented from the paper's Figure 4. Note the patent history in the
 * README's Notes before using this commercially.
 */

export interface ArcParams {
  /** Maximum number of entries held. */
  capacity: number;
}

const DEFAULT_CAPACITY = 1000;
const NIL = -1;

/** The four lists, used to index the head/tail/length arrays. */
const T1 = 0;
const T2 = 1;
const B1 = 2;
const B2 = 3;

/** Where a key sits. Returned by {@link Arc.listOfKey}. */
export type ArcList = "t1" | "t2" | "b1" | "b2" | "absent";

const LIST_NAMES: ArcList[] = ["t1", "t2", "b1", "b2"];

export default class Arc<K> {
  private readonly capacity: number;

  private readonly index: Map<K, number>;
  private readonly keys: (K | undefined)[];

  // One slot pool shared by all four lists; a slot is in exactly one of them.
  private readonly next: Int32Array;
  private readonly prev: Int32Array;
  private readonly listOf: Int8Array;
  private readonly heads: Int32Array;
  private readonly tails: Int32Array;
  private readonly lengths: Int32Array;

  private readonly freeSlots: Int32Array;
  private freeCount: number;

  /** Target size for T1. Adapts on every ghost hit. */
  private target = 0;

  /**
   * The key ARC has decided to evict, waiting for the caller to ask for it.
   *
   * ARC decides what to replace while handling the miss — the decision depends
   * on list sizes *before* the new key is inserted — but the interface asks for
   * the victim afterwards. So the decision is executed at once and the key held
   * here until `evict()` collects it.
   */
  private pendingVictim: K | undefined = undefined;

  constructor(params: Partial<ArcParams> = {}) {
    const capacity = params.capacity ?? DEFAULT_CAPACITY;
    if (!Number.isInteger(capacity) || capacity < 1) {
      throw new RangeError(`Arc: capacity must be a positive integer, received ${capacity}`);
    }

    this.capacity = capacity;
    // The cache holds at most c entries and the ghosts at most c more, plus a
    // spare for the entry in flight during a miss.
    const slots = 2 * capacity + 2;

    this.index = new Map<K, number>();
    this.keys = new Array<K | undefined>(slots);
    this.next = new Int32Array(slots).fill(NIL);
    this.prev = new Int32Array(slots).fill(NIL);
    this.listOf = new Int8Array(slots).fill(-1);
    this.heads = new Int32Array(4).fill(NIL);
    this.tails = new Int32Array(4).fill(NIL);
    this.lengths = new Int32Array(4);
    this.freeSlots = new Int32Array(slots);
    for (let slot = 0; slot < slots; slot += 1) this.freeSlots[slot] = slots - 1 - slot;
    this.freeCount = slots;
  }

  onAccess(key: K, hit: boolean): void {
    const existing = this.index.get(key);

    if (hit) {
      if (existing === undefined || this.listOf[existing]! > T2) {
        throw new Error(`Arc: onAccess reported a hit for a key it does not hold: ${String(key)}`);
      }
      // Case I: a second recent use promotes the key to the frequent list.
      this.unlink(existing);
      this.linkFront(T2, existing);
      return;
    }

    if (existing !== undefined) {
      const list = this.listOf[existing]!;
      if (list <= T2) {
        throw new Error(`Arc: onAccess reported a miss for a resident key: ${String(key)}`);
      }

      const b1 = this.lengths[B1]!;
      const b2 = this.lengths[B2]!;

      if (list === B1) {
        // Case II. A key evicted from the recent list is back: recency was
        // undervalued, so give T1 more room. The step is larger when B1 is the
        // smaller ghost list, because the evidence is proportionally stronger.
        const delta = b1 >= b2 ? 1 : Math.floor(b2 / b1);
        this.target = Math.min(this.capacity, this.target + delta);
      } else {
        // Case III. A key evicted from the frequent list is back: frequency was
        // undervalued, so take room away from T1.
        const delta = b2 >= b1 ? 1 : Math.floor(b1 / b2);
        this.target = Math.max(0, this.target - delta);
      }

      this.replace(list === B2);

      // The key returns to the cache in the frequent list: it has now been
      // seen twice, whatever happened in between.
      this.unlink(existing);
      this.linkFront(T2, existing);
      return;
    }

    // Case IV: a key ARC has no memory of at all.
    const t1 = this.lengths[T1]!;
    const b1 = this.lengths[B1]!;
    const total = t1 + this.lengths[T2]! + b1 + this.lengths[B2]!;

    if (t1 + b1 === this.capacity) {
      if (t1 < this.capacity) {
        this.dropOldestGhost(B1);
        this.replace(false);
      } else {
        // T1 is the whole cache and there are no recent ghosts, so the oldest
        // recent entry goes without leaving one — there would be nowhere to
        // put it.
        this.evictOldest(T1, null);
      }
    } else if (t1 + b1 < this.capacity && total >= this.capacity) {
      if (total === 2 * this.capacity) this.dropOldestGhost(B2);
      this.replace(false);
    }

    const slot = this.takeSlot(key);
    this.linkFront(T1, slot);
  }

  evict(): K {
    if (this.pendingVictim === undefined) {
      throw new Error(
        "Arc: evict() called when no replacement was scheduled. ARC chooses its victim while " +
          "handling the miss, so evict() is only valid once the cache is over capacity.",
      );
    }
    const key = this.pendingVictim;
    this.pendingVictim = undefined;
    return key;
  }

  /** Entries currently held, including one already chosen for eviction. */
  size(): number {
    return this.lengths[T1]! + this.lengths[T2]! + (this.pendingVictim === undefined ? 0 : 1);
  }

  /** The current target size for T1. Not part of the interface; used by tests. */
  targetT1(): number {
    return this.target;
  }

  /** Which list a key is in. Not part of the interface; used by tests. */
  listOfKey(key: K): ArcList {
    const slot = this.index.get(key);
    if (slot === undefined) return "absent";
    return LIST_NAMES[this.listOf[slot]!] ?? "absent";
  }

  /**
   * The paper's REPLACE: choose a victim from T1 or T2 and demote it to the
   * matching ghost list.
   *
   * The rule reads oddly until you see what it is doing. T1 gives up an entry
   * when it is over its target — or when it is exactly at target and the key
   * that caused this came back from B2, which is a hint that the frequent side
   * deserves the benefit of the doubt.
   */
  private replace(returningFromB2: boolean): void {
    const t1 = this.lengths[T1]!;
    const takeFromT1 = t1 >= 1 && ((returningFromB2 && t1 === this.target) || t1 > this.target);

    if (takeFromT1) this.evictOldest(T1, B1);
    else this.evictOldest(T2, B2);
  }

  /** Remove the oldest entry of a cache list, optionally leaving a ghost. */
  private evictOldest(list: number, ghost: number | null): void {
    const slot = this.tails[list]!;
    if (slot === NIL) {
      throw new Error("Arc: evict() called with nothing resident");
    }

    const key = this.keys[slot] as K;
    this.unlink(slot);

    if (ghost === null) {
      this.releaseSlot(slot, key);
    } else {
      this.linkFront(ghost, slot);
    }

    this.pendingVictim = key;
  }

  private dropOldestGhost(list: number): void {
    const slot = this.tails[list]!;
    if (slot === NIL) return;
    const key = this.keys[slot] as K;
    this.unlink(slot);
    this.releaseSlot(slot, key);
  }

  private takeSlot(key: K): number {
    if (this.freeCount === 0) {
      throw new Error(
        `Arc: ran out of slots at capacity ${this.capacity}, which should be impossible.`,
      );
    }
    this.freeCount -= 1;
    const slot = this.freeSlots[this.freeCount]!;
    this.keys[slot] = key;
    this.index.set(key, slot);
    return slot;
  }

  private releaseSlot(slot: number, key: K): void {
    this.index.delete(key);
    this.keys[slot] = undefined;
    this.freeSlots[this.freeCount] = slot;
    this.freeCount += 1;
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
}
