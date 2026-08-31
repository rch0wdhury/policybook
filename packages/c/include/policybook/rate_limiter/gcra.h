/*
 * GENERATED COPY — do not edit. Edit policies/rate-limiter/gcra/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * Gcra — the token bucket, kept as one number instead of three.
 *
 * The Generic Cell Rate Algorithm stores a single theoretical arrival time per
 * key: the instant at which the next request would be exactly on schedule. How
 * many permits are banked, and how much of the next one has accrued, are both
 * implied by how far that instant sits from now.
 *
 *     #include <policybook/rate_limiter/rate_limiter.h>
 *     #include <policybook/rate_limiter/gcra.h>
 *
 *     pb_ratelimiter_gcra_params params = PB_RATELIMITER_GCRA_PARAMS_DEFAULT;
 *     pb_ratelimiter *limiter = pb_ratelimiter_gcra.create(&params, NULL, NULL);
 *     if (pb_ratelimiter_gcra.allow(limiter, key, 1u, now_ms)) { ... }
 *     pb_ratelimiter_gcra.destroy(limiter);
 *
 * It admits and refuses exactly what `pb_ratelimiter_token_bucket` does,
 * including the fractional carry, with `retry_after` agreeing to the
 * millisecond. The reason to choose it is state: **one uint64 per key** rather
 * than three fields.
 *
 * The arithmetic is exact integers in units scaled by the rate. The textbook
 * form needs an emission interval of `1000 / rate_per_sec` milliseconds, which
 * is not whole for most rates; multiplying through by the rate clears it, so
 * one permit costs exactly 1,000 units and one millisecond is `rate_per_sec` of
 * them.
 *
 * **`max_keys` is a C-only parameter.** The TypeScript and Python ports grow a
 * hash map without limit; C takes all its memory in `create` and never
 * allocates again (concept.md 12.2). Once the table is full a key that has
 * never been seen is refused — fail-closed.
 *
 * Memory: 8 bytes per tracked key, plus the map.
 */

#ifndef POLICYBOOK_RATE_LIMITER_GCRA_H
#define POLICYBOOK_RATE_LIMITER_GCRA_H

#include <stdint.h>

#include "policybook/rate_limiter/rate_limiter.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_ratelimiter_gcra_params {
    uint32_t rate_per_sec; /* permits per second, sustained */
    uint32_t burst;        /* permits spendable at once after an idle period */
    uint32_t max_keys;     /* C only: keys tracked before new ones are refused */
} pb_ratelimiter_gcra_params;

#define PB_RATELIMITER_GCRA_PARAMS_DEFAULT { 100u, 100u, 1024u }

extern const pb_ratelimiter_vtable pb_ratelimiter_gcra;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_RATE_LIMITER_GCRA_H */
