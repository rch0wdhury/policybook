/*
 * The `cache` domain: eviction for a fixed-capacity key cache.
 *
 * A cache holds at most `capacity` keys. When a new key arrives and the cache is
 * full, something has to go, and which one is the whole question. A policy
 * observes every lookup and names a victim when asked.
 *
 * Every policy exports a `const pb_cache_vtable` and a params struct with a
 * _DEFAULT initialiser, so a caller can swap policies at runtime by pointing at
 * a different vtable, or call one policy's functions directly:
 *
 *     #include <policybook/cache/cache.h>
 *     #include <policybook/cache/sieve.h>
 *
 *     pb_cache_sieve_params params = PB_CACHE_SIEVE_PARAMS_DEFAULT;
 *     pb_cache *cache = pb_cache_sieve.create(&params, NULL, NULL);
 *     pb_cache_sieve.on_access(cache, key, false, NULL);
 *     uint64_t victim = pb_cache_sieve.evict(cache);
 *     pb_cache_sieve.destroy(cache);
 *
 * Keys are uint64_t: callers hash their own keys. Time is passed in, never read
 * from a clock. Randomness comes from the pb_rng handed to create, never rand().
 */

#ifndef POLICYBOOK_CACHE_CACHE_H
#define POLICYBOOK_CACHE_CACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "policybook/allocator.h"
#include "policybook/rng.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque per-policy state. */
typedef struct pb_cache pb_cache;

/* Extra information a policy may use, supplied by the caller. */
typedef struct pb_cache_meta {
    uint64_t size; /* entry size, for size-aware policies; 1 if unused */
    uint64_t now;  /* domain time, in arbitrary units; never wall-clock */
} pb_cache_meta;

typedef struct pb_cache_vtable {
    /*
     * Allocate the policy and everything it will ever need.
     *
     * `params` points at the policy's own params struct. `allocator` may be
     * NULL for malloc/free. `rng` may be NULL for a policy that needs no
     * randomness; a policy that does need it must document that.
     *
     * Returns NULL if allocation fails or the params are invalid.
     */
    pb_cache *(*create)(const void *params, const pb_allocator *allocator, pb_rng *rng);

    /* Called on every lookup, before insertion on a miss. `meta` may be NULL. */
    void (*on_access)(pb_cache *cache, uint64_t key, bool hit, const pb_cache_meta *meta);

    /* Called when capacity is exceeded. Returns a key the policy currently holds. */
    uint64_t (*evict)(pb_cache *cache);

    /* Optional admission control; NULL if the policy admits everything. */
    bool (*admit)(pb_cache *cache, uint64_t key, const pb_cache_meta *meta);

    /* Release everything create allocated. */
    void (*destroy)(pb_cache *cache);

    /* Bytes held, so a caller can budget. */
    size_t (*memory_bytes)(const pb_cache *cache);

    /*
     * True if the policy allocates after create.
     *
     * No v0.1 policy may set this. It exists so that one which cannot honour
     * the contract has to say so in its vtable rather than quietly breaking a
     * caller's memory budget.
     */
    bool allocates_after_create;

    /* Policy id, e.g. "cache/sieve". */
    const char *id;
} pb_cache_vtable;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_CACHE_CACHE_H */
