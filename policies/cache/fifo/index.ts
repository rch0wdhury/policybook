/**
 * FIFO — evict the key that arrived first.
 *
 * The baseline every other cache policy is measured against. It ignores hits
 * entirely: a key's position is fixed the moment it enters, so however often it
 * is used, it leaves in arrival order.
 *
 * That sounds obviously bad, and on most workloads it is. It is here because a
 * benchmark needs a floor, because FIFO is genuinely the right answer when
 * eviction must be predictable or lock-free, and because several modern
 * policies (S3-FIFO, SIEVE) are FIFO queues with one extra bit — which is much
 * easier to see once you have read this file.
 */

export interface FifoParams {
  /** Maximum number of entries held. */
  capacity: number;
}

const DEFAULT_CAPACITY = 1000;

export default class Fifo<K> {
  private readonly capacity: number;

  /**
   * Resident keys in arrival order, as a circular buffer.
   *
   * Sized `capacity + 1` because a caller inserts before evicting, so the queue
   * is briefly one over capacity.
   */
  private readonly slots: (K | undefined)[];
  private head = 0;
  private length = 0;

  constructor(params: Partial<FifoParams> = {}) {
    const capacity = params.capacity ?? DEFAULT_CAPACITY;
    if (!Number.isInteger(capacity) || capacity < 1) {
      throw new RangeError(`Fifo: capacity must be a positive integer, received ${capacity}`);
    }
    this.capacity = capacity;
    this.slots = new Array<K | undefined>(capacity + 1);
  }

  onAccess(key: K, hit: boolean): void {
    // The whole policy: a hit changes nothing. FIFO does not learn.
    if (hit) return;

    if (this.length >= this.slots.length) {
      throw new Error(
        `Fifo: ${this.length} entries inserted without an evict, capacity is ${this.capacity}. ` +
          "Call evict() once the cache is over capacity.",
      );
    }

    let index = this.head + this.length;
    if (index >= this.slots.length) index -= this.slots.length;
    this.slots[index] = key;
    this.length += 1;
  }

  evict(): K {
    if (this.length === 0) {
      throw new Error("Fifo: evict() called with nothing resident");
    }

    const key = this.slots[this.head] as K;
    this.slots[this.head] = undefined;
    this.head += 1;
    if (this.head >= this.slots.length) this.head = 0;
    this.length -= 1;
    return key;
  }

  /** Entries currently held. Not part of the interface; used by tests. */
  size(): number {
    return this.length;
  }
}
