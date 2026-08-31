/*
 * GENERATED COPY — do not edit. Edit policies/cache/lfu/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * LFU — evict the key used least often.
 *
 * The O(1) construction of Shah, Mitra and Matani: entries are grouped into
 * frequency classes, the classes form an ascending linked list, and neither
 * promotion nor eviction ever scans.
 *
 *     #include <policybook/cache/cache.h>
 *     #include <policybook/cache/lfu.h>
 *
 *     pb_cache_lfu_params params = PB_CACHE_LFU_PARAMS_DEFAULT;
 *     params.capacity = 1024;
 *     pb_cache *cache = pb_cache_lfu.create(&params, NULL, NULL);
 *
 * Memory: roughly 72 bytes per entry — 8 for the key, 12 for the entry links
 * and class index, 4 for the free stack, about 24 amortised for the hash table,
 * and 24 for the class pool, which is sized to the entry count because there
 * cannot be more distinct frequencies than entries. Call `memory_bytes` for the
 * exact figure.
 *
 * Tie-break: within a frequency class, the entry that reached that frequency
 * earliest is evicted first.
 */

#ifndef POLICYBOOK_CACHE_LFU_H
#define POLICYBOOK_CACHE_LFU_H

#include <stdint.h>

#include "policybook/cache/cache.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_cache_lfu_params {
    uint32_t capacity; /* maximum number of entries held */
} pb_cache_lfu_params;

#define PB_CACHE_LFU_PARAMS_DEFAULT { 1000u }

extern const pb_cache_vtable pb_cache_lfu;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_CACHE_LFU_H */
