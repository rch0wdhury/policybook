/*
 * LeakyBucket — a level that rises with each request and drains at a steady rate.
 *
 * The meter formulation: every admitted request adds one unit to a bucket that
 * leaks at `rate_per_sec`, and a request that would overflow `capacity` is
 * refused. At the default capacity of 1 that means exact spacing — one request
 * every 1000/rate_per_sec milliseconds, never two together.
 *
 *     #include <policybook/rate_limiter/rate_limiter.h>
 *     #include <policybook/rate_limiter/leaky_bucket.h>
 *
 *     pb_ratelimiter_leaky_bucket_params params =
 *         PB_RATELIMITER_LEAKY_BUCKET_PARAMS_DEFAULT;
 *     pb_ratelimiter *limiter = pb_ratelimiter_leaky_bucket.create(&params, NULL, NULL);
 *     if (pb_ratelimiter_leaky_bucket.allow(limiter, key, 1u, now_ms)) { ... }
 *     pb_ratelimiter_leaky_bucket.destroy(limiter);
 *
 * At equal parameters this is `pb_ratelimiter_token_bucket` under the
 * substitution `tokens = capacity - level`. Both ship because the names are
 * what people look for; neither is faster. The default capacity is 1 rather
 * than the domain's reference burst of 100, because a caller who wants a burst
 * allowance is describing a token bucket.
 *
 * **`max_keys` is a C-only parameter.** The TypeScript and Python ports grow a
 * hash map without limit; C takes all its memory in `create` and never
 * allocates again. Once the table is full a key that has
 * never been seen is refused — fail-closed.
 *
 * Memory: 24 bytes per tracked key, plus the map.
 */

#ifndef POLICYBOOK_RATE_LIMITER_LEAKY_BUCKET_H
#define POLICYBOOK_RATE_LIMITER_LEAKY_BUCKET_H

#include <stdint.h>

#include "policybook/rate_limiter/rate_limiter.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_ratelimiter_leaky_bucket_params {
    uint32_t rate_per_sec; /* units drained per second */
    uint32_t capacity;     /* maximum level, and so the largest burst that fits */
    uint32_t max_keys;     /* C only: keys tracked before new ones are refused */
} pb_ratelimiter_leaky_bucket_params;

#define PB_RATELIMITER_LEAKY_BUCKET_PARAMS_DEFAULT { 100u, 1u, 1024u }

extern const pb_ratelimiter_vtable pb_ratelimiter_leaky_bucket;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_RATE_LIMITER_LEAKY_BUCKET_H */
