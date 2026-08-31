/*
 * GENERATED COPY — do not edit. Edit policies/rate-limiter/token-bucket/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * TokenBucket — spend from a balance that refills at a steady rate.
 *
 * The domain's recommended default. A key holds up to `burst` tokens; each
 * request spends one; the balance refills at `rate_per_sec` and stops at
 * `burst`. A long-run ceiling of `rate_per_sec`, plus the ability for a caller
 * who has been quiet to spend what it saved.
 *
 *     #include <policybook/rate_limiter/rate_limiter.h>
 *     #include <policybook/rate_limiter/token_bucket.h>
 *
 *     pb_ratelimiter_token_bucket_params params =
 *         PB_RATELIMITER_TOKEN_BUCKET_PARAMS_DEFAULT;
 *     pb_ratelimiter *limiter = pb_ratelimiter_token_bucket.create(&params, NULL, NULL);
 *     if (pb_ratelimiter_token_bucket.allow(limiter, key, 1u, now_ms)) { ... }
 *     pb_ratelimiter_token_bucket.destroy(limiter);
 *
 * The ledger is integer arithmetic: tokens are whole and the fraction lives in
 * a carry measured in thousandths of a token, matching the TypeScript and
 * Python ports exactly.
 *
 * At equal parameters this is `pb_ratelimiter_leaky_bucket` under the
 * substitution `tokens = capacity - level`. Both ship because the names are
 * what people look for; neither is faster.
 *
 * **`max_keys` is a C-only parameter.** The TypeScript and Python ports grow a
 * hash map without limit; C takes all its memory in `create` and never
 * allocates again. Once the table is full a key that has
 * never been seen is refused — fail-closed, because the alternative is to stop
 * limiting the moment an attacker cycles through keys.
 *
 * Memory: 24 bytes per tracked key, plus the map.
 */

#ifndef POLICYBOOK_RATE_LIMITER_TOKEN_BUCKET_H
#define POLICYBOOK_RATE_LIMITER_TOKEN_BUCKET_H

#include <stdint.h>

#include "policybook/rate_limiter/rate_limiter.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_ratelimiter_token_bucket_params {
    uint32_t rate_per_sec; /* tokens added per second */
    uint32_t burst;        /* maximum tokens a key can hold */
    uint32_t max_keys;     /* C only: keys tracked before new ones are refused */
} pb_ratelimiter_token_bucket_params;

#define PB_RATELIMITER_TOKEN_BUCKET_PARAMS_DEFAULT { 100u, 100u, 1024u }

extern const pb_ratelimiter_vtable pb_ratelimiter_token_bucket;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_RATE_LIMITER_TOKEN_BUCKET_H */
