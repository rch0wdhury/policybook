/*
 * 2Q — admit to the main cache only on a second access.
 *
 * New keys audition in A1in, a small FIFO. Keys evicted from it leave their
 * identifier in A1out, a ghost queue holding keys but no values. A key that
 * returns while its ghost is live has proven reuse and enters Am, the main LRU.
 * A scan therefore never reaches Am.
 *
 *     #include <policybook/cache/cache.h>
 *     #include <policybook/cache/2q.h>
 *
 *     pb_cache_2q_params params = PB_CACHE_2Q_PARAMS_DEFAULT;
 *     params.capacity = 1024;
 *     pb_cache *cache = pb_cache_2q.create(&params, NULL, NULL);
 *
 * Memory: about 69 bytes per entry at the default fractions — the resident
 * entries cost roughly what LRU's do, plus about 22 for the A1out ghost ring
 * and its membership table. Call `memory_bytes` for the exact figure.
 *
 * Note that a hit on a key still in A1in does nothing. That is the algorithm:
 * promotion requires a second access *after* eviction, not a repeat reference.
 */

#ifndef POLICYBOOK_CACHE_2Q_H
#define POLICYBOOK_CACHE_2Q_H

#include <stdint.h>

#include "policybook/cache/cache.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_cache_2q_params {
    uint32_t capacity; /* maximum number of entries held */
    double kin;        /* fraction of capacity given to A1in */
    double kout;       /* fraction of capacity worth of keys remembered in A1out */
} pb_cache_2q_params;

#define PB_CACHE_2Q_PARAMS_DEFAULT { 1000u, 0.25, 0.5 }

extern const pb_cache_vtable pb_cache_2q;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_CACHE_2Q_H */
