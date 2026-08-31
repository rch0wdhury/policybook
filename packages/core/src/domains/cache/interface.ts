/**
 * The `cache` domain: eviction for a fixed-capacity key cache.
 *
 * The smallest interesting decision problem in the registry. A cache holds at
 * most `capacity` keys; when a new key arrives and the cache is full, something
 * has to go. Which one is the whole question, and the answer is worth a great
 * deal — the difference between a good and a bad eviction rule on the same
 * workload is routinely ten points of hit rate.
 *
 * The interface is deliberately tiny (concept.md §5.1): a policy observes every
 * lookup and names a victim when asked. It never sees the cached values, never
 * reads the clock, and never allocates on the hot path.
 */

/** Extra information a policy may use, supplied by the harness. */
export interface CacheMeta {
  /** Entry size, for size-aware policies. Defaults to 1. */
  size?: number;
  /** Domain time, in arbitrary units. Never wall-clock. */
  now?: number;
}

/** Every cache policy implements this, and nothing more. */
export interface CachePolicy<K> {
  /**
   * Called on every lookup, before insertion on a miss.
   *
   * `hit` says whether the key was resident. A policy learns everything it
   * knows from this call.
   */
  onAccess(key: K, hit: boolean, meta?: CacheMeta): void;

  /**
   * Called when capacity is exceeded. Returns the key to remove.
   *
   * The returned key must currently be resident; the harness treats anything
   * else as a bug in the policy.
   */
  evict(): K;

  /**
   * Optional admission control. Return false to skip inserting the key.
   *
   * Policies like W-TinyLFU use this to keep a one-hit wonder from displacing
   * something valuable.
   */
  admit?(key: K, meta?: CacheMeta): boolean;
}

/** Every cache policy takes at least a capacity. */
export interface CacheParams {
  /** Maximum number of entries held. */
  capacity: number;
}
