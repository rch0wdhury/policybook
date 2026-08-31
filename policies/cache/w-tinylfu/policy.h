/*
 * W-TinyLFU — frequency-based admission on four bits per counter.
 *
 * Approximate counts for far more keys than the cache holds, kept in a
 * fixed-size count-min sketch and halved periodically so the estimate follows
 * the workload. That makes frequency cheap enough to use for admission rather
 * than only eviction: an unpopular newcomer never displaces a proven entry.
 *
 * A small window LRU absorbs new arrivals; when it overflows, its victim's
 * estimated frequency is compared with the main cache's victim, and only the
 * more popular survives. The main cache is a segmented LRU.
 *
 *     #include <policybook/cache/cache.h>
 *     #include <policybook/cache/w_tinylfu.h>
 *
 *     pb_cache_w_tinylfu_params params = PB_CACHE_W_TINYLFU_PARAMS_DEFAULT;
 *     params.capacity = 1024;
 *     pb_cache *cache = pb_cache_w_tinylfu.create(&params, NULL, NULL);
 *
 * Memory: about 65 bytes per entry — the entries themselves cost roughly what
 * LRU's do, plus four bytes of sketch and one bit of doorkeeper per entry. Call
 * `memory_bytes` for the exact figure.
 *
 * Keys are hashed directly by the sketch, so a caller with non-integer keys
 * hashes its own, exactly as the rest of this API expects.
 */

#ifndef POLICYBOOK_CACHE_W_TINYLFU_H
#define POLICYBOOK_CACHE_W_TINYLFU_H

#include <stdint.h>

#include "policybook/cache/cache.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_cache_w_tinylfu_params {
    uint32_t capacity;         /* maximum number of entries held */
    double window_fraction;    /* fraction of capacity given to the admission window */
    double protected_fraction; /* fraction of the main cache reserved for protected entries */
} pb_cache_w_tinylfu_params;

#define PB_CACHE_W_TINYLFU_PARAMS_DEFAULT { 1000u, 0.01, 0.8 }

extern const pb_cache_vtable pb_cache_w_tinylfu;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_CACHE_W_TINYLFU_H */
