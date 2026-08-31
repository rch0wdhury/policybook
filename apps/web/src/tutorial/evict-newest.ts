/**
 * EvictNewest — a deliberately bad cache policy, for the tutorial.
 *
 * It does the simplest thing that satisfies the interface: when the cache is
 * full, throw out whatever arrived most recently. That is very nearly the worst
 * possible rule, and that is the point — a policy you can hold in your head,
 * whose failure you can predict before running it, is the right thing to write
 * first.
 *
 * It lives here rather than in `policies/` because it is a teaching example,
 * not a registry entry: it has no paper behind it, no vectors, and nobody
 * should `policybook add` it. The tutorial runs *this file* and shows *this
 * file*, so what a reader sees and what they watch run cannot differ.
 */

/** Keys are numbers here, as they are throughout the cache harness. */
export default class EvictNewest {
  private readonly capacity: number;
  /** Insertion order, oldest first. */
  private readonly order: number[] = [];

  constructor(params: { capacity: number }) {
    this.capacity = params.capacity;
  }

  /**
   * Called for every request, hit or miss.
   *
   * On a hit there is nothing to do: the key is already resident and this
   * policy does not care how often or how recently anything is used. On a miss
   * the harness is about to insert the key, so it goes on the end.
   */
  onAccess(key: number, hit: boolean): void {
    if (!hit) this.order.push(key);
  }

  /**
   * Called when the cache is over capacity. Return the key to drop.
   *
   * The newest is the one we just inserted, which means this policy spends
   * every eviction undoing the insert that caused it. The cache therefore
   * settles at `capacity` items and then almost never changes, which is exactly
   * what the runner shows.
   */
  evict(): number {
    const victim = this.order.pop();
    if (victim === undefined) throw new Error("evict() with nothing resident");
    return victim;
  }

  /** Optional introspection, the same method SIEVE exposes. Unused here. */
  sizeOf(): number {
    return this.order.length;
  }
}
