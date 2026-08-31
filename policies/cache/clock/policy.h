/*
 * CLOCK — approximate LRU with one reference bit per entry.
 *
 * A hit sets a bit and writes nothing shared, so concurrent readers need no
 * lock. Eviction walks a hand around the entries in arrival order, sparing
 * those whose bit is set and clearing it as it passes.
 *
 *     #include <policybook/cache/cache.h>
 *     #include <policybook/cache/clock.h>
 *
 *     pb_cache_clock_params params = PB_CACHE_CLOCK_PARAMS_DEFAULT;
 *     params.capacity = 1024;
 *     pb_cache *cache = pb_cache_clock.create(&params, NULL, NULL);
 *
 * Memory: about 44 bytes per entry — 8 for the key, 1 for the reference bit,
 * 4 for the order ring, 4 for the free stack, and roughly 27 for the hash
 * table. Call `memory_bytes` for the exact figure.
 *
 * Note that a single eviction may walk every entry when all bits are set. The
 * amortised cost is O(1), but the worst-case latency of one call is O(n).
 */

#ifndef POLICYBOOK_CACHE_CLOCK_H
#define POLICYBOOK_CACHE_CLOCK_H

#include <stdint.h>

#include "policybook/cache/cache.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_cache_clock_params {
    uint32_t capacity; /* maximum number of entries held */
} pb_cache_clock_params;

#define PB_CACHE_CLOCK_PARAMS_DEFAULT { 1000u }

extern const pb_cache_vtable pb_cache_clock;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_CACHE_CLOCK_H */
