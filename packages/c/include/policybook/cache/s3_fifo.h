/*
 * GENERATED COPY — do not edit. Edit policies/cache/s3-fifo/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * S3-FIFO — three FIFO queues, two bits per entry, no list surgery.
 *
 * New keys audition in S, a small FIFO holding a tenth of the cache. An object
 * reused while it sits there is promoted to M, the main FIFO; one that is not
 * falls out and leaves its key in G, a ghost queue with no values behind it. A
 * key returning while its ghost is live skips the audition. Inside M a two-bit
 * counter grants up to three second chances.
 *
 *     #include <policybook/cache/cache.h>
 *     #include <policybook/cache/s3_fifo.h>
 *
 *     pb_cache_s3_fifo_params params = PB_CACHE_S3_FIFO_PARAMS_DEFAULT;
 *     params.capacity = 1024;
 *     pb_cache *cache = pb_cache_s3_fifo.create(&params, NULL, NULL);
 *
 * Memory: about 83 bytes per entry — a key, a two-bit counter and a queue slot
 * per entry, plus the ghost ring and its membership table. Call `memory_bytes`
 * for the exact figure.
 *
 * A hit is a single counter increment: nothing moves, and no other thread needs
 * to observe it. That is what makes this scale where LRU does not.
 */

#ifndef POLICYBOOK_CACHE_S3_FIFO_H
#define POLICYBOOK_CACHE_S3_FIFO_H

#include <stdint.h>

#include "policybook/cache/cache.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_cache_s3_fifo_params {
    uint32_t capacity;     /* maximum number of entries held */
    double small_fraction; /* fraction of capacity given to the small queue */
} pb_cache_s3_fifo_params;

#define PB_CACHE_S3_FIFO_PARAMS_DEFAULT { 1000u, 0.1 }

extern const pb_cache_vtable pb_cache_s3_fifo;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_CACHE_S3_FIFO_H */
