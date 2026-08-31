/*
 * GENERATED COPY — do not edit. Edit policies/cache/lru/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * LRU — evict the key used longest ago.
 *
 * The default baseline. A hash map from key to slot, and a doubly linked list
 * over those slots in recency order.
 *
 *     #include <policybook/cache/cache.h>
 *     #include <policybook/cache/lru.h>
 *
 *     pb_cache_lru_params params = PB_CACHE_LRU_PARAMS_DEFAULT;
 *     params.capacity = 1024;
 *     pb_cache *cache = pb_cache_lru.create(&params, NULL, NULL);
 *
 * Memory: roughly 40 bytes per entry — 8 for the key, 8 for the two list links,
 * and about 24 amortised for the hash table, which is sized at twice the entry
 * count to keep probe chains short. Call `memory_bytes` for the exact figure.
 *
 * Note that every hit writes to the recency list. That is LRU's real
 * operational cost: readers cannot share the cache without synchronisation.
 * CLOCK and SIEVE approximate LRU without writing on the hit path.
 */

#ifndef POLICYBOOK_CACHE_LRU_H
#define POLICYBOOK_CACHE_LRU_H

#include <stdint.h>

#include "policybook/cache/cache.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_cache_lru_params {
    uint32_t capacity; /* maximum number of entries held */
} pb_cache_lru_params;

#define PB_CACHE_LRU_PARAMS_DEFAULT { 1000u }

extern const pb_cache_vtable pb_cache_lru;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_CACHE_LRU_H */
