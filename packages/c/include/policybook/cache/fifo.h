/*
 * GENERATED COPY — do not edit. Edit policies/cache/fifo/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * FIFO — evict the key that arrived first.
 *
 * The baseline every other cache policy is measured against. It ignores hits
 * entirely, so a hit costs nothing and touches no shared state — the one place
 * FIFO genuinely beats LRU.
 *
 *     #include <policybook/cache/cache.h>
 *     #include <policybook/cache/fifo.h>
 *
 *     pb_cache_fifo_params params = PB_CACHE_FIFO_PARAMS_DEFAULT;
 *     params.capacity = 1024;
 *     pb_cache *cache = pb_cache_fifo.create(&params, NULL, NULL);
 *     pb_cache_fifo.on_access(cache, key, hit, NULL);
 *     uint64_t victim = pb_cache_fifo.evict(cache);
 *     pb_cache_fifo.destroy(cache);
 *
 * Memory: 8 bytes per entry (one key), plus the struct. Nothing else — no
 * links, no counters, no map. The cheapest policy in the domain.
 */

#ifndef POLICYBOOK_CACHE_FIFO_H
#define POLICYBOOK_CACHE_FIFO_H

#include <stdint.h>

#include "policybook/cache/cache.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_cache_fifo_params {
    uint32_t capacity; /* maximum number of entries held */
} pb_cache_fifo_params;

#define PB_CACHE_FIFO_PARAMS_DEFAULT { 1000u }

extern const pb_cache_vtable pb_cache_fifo;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_CACHE_FIFO_H */
