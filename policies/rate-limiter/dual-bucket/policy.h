/*
 * DualBucket — two limits at once, and a request must satisfy both.
 *
 * A requests-per-minute ceiling and a tokens-per-minute ceiling, checked
 * together. One dimension counts calls, the other counts how much work each
 * call asks for, and either can refuse on its own. This is the shape every LLM
 * API uses.
 *
 *     #include <policybook/rate_limiter/rate_limiter.h>
 *     #include <policybook/rate_limiter/dual_bucket.h>
 *
 *     pb_ratelimiter_dual_bucket_params params =
 *         PB_RATELIMITER_DUAL_BUCKET_PARAMS_DEFAULT;
 *     pb_ratelimiter *limiter = pb_ratelimiter_dual_bucket.create(&params, NULL, NULL);
 *     // `cost` is the work the call asks for; it always charges one request.
 *     if (pb_ratelimiter_dual_bucket.allow(limiter, key, token_count, now_ms)) { ... }
 *     pb_ratelimiter_dual_bucket.destroy(limiter);
 *
 * Each dimension is a token bucket with a per-minute period. **The charge is
 * atomic**: if either dimension would refuse, neither is charged, so a caller
 * refused for work has not quietly spent a request too.
 *
 * `retry_after` reports the later of the two dimensions and assumes a smallest
 * possible call, since the vtable has no cost argument for it.
 *
 * **`max_keys` is a C-only parameter.** The TypeScript and Python ports grow a
 * hash map without limit; C takes all its memory in `create` and never
 * allocates again. Once the table is full a key that has
 * never been seen is refused — fail-closed.
 *
 * Memory: 24 bytes per tracked key, plus the map.
 */

#ifndef POLICYBOOK_RATE_LIMITER_DUAL_BUCKET_H
#define POLICYBOOK_RATE_LIMITER_DUAL_BUCKET_H

#include <stdint.h>

#include "policybook/rate_limiter/rate_limiter.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_ratelimiter_dual_bucket_params {
    uint32_t requests_per_min; /* calls allowed per minute, regardless of size */
    uint32_t tokens_per_min;   /* units of work allowed per minute */
    uint32_t max_keys;         /* C only: keys tracked before new ones are refused */
} pb_ratelimiter_dual_bucket_params;

#define PB_RATELIMITER_DUAL_BUCKET_PARAMS_DEFAULT { 500u, 200000u, 1024u }

extern const pb_ratelimiter_vtable pb_ratelimiter_dual_bucket;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_RATE_LIMITER_DUAL_BUCKET_H */
