/*
 * GENERATED COPY — do not edit. Edit policies/cache/arc/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * ARC — balance recency against frequency, and tune the balance itself.
 *
 * Four lists: T1 for keys seen once recently, T2 for keys seen at least twice,
 * and ghost lists B1 and B2 holding the identifiers of keys evicted from each.
 * A target p says how much of the cache T1 should get, and every ghost hit
 * moves it: a hit in B1 means recency was undervalued, a hit in B2 means
 * frequency was.
 *
 *     #include <policybook/cache/cache.h>
 *     #include <policybook/cache/arc.h>
 *
 *     pb_cache_arc_params params = PB_CACHE_ARC_PARAMS_DEFAULT;
 *     params.capacity = 1024;
 *     pb_cache *cache = pb_cache_arc.create(&params, NULL, NULL);
 *
 * Memory: about 96 bytes per entry — more than twice LRU's, because ARC tracks
 * up to two keys for every entry it caches. Call `memory_bytes` for the exact
 * figure.
 *
 * There is deliberately no tuning parameter beyond the capacity.
 *
 * See the README's Notes section for the patent history before commercial use.
 */

#ifndef POLICYBOOK_CACHE_ARC_H
#define POLICYBOOK_CACHE_ARC_H

#include <stdint.h>

#include "policybook/cache/cache.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_cache_arc_params {
    uint32_t capacity; /* maximum number of entries held */
} pb_cache_arc_params;

#define PB_CACHE_ARC_PARAMS_DEFAULT { 1000u }

extern const pb_cache_vtable pb_cache_arc;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_CACHE_ARC_H */
