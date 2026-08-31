/*
 * GENERATED COPY — do not edit. Edit policies/rate-limiter/sliding-log/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * SlidingLog — remember every request time, and count the recent ones.
 *
 * The only limiter in this domain that enforces its limit exactly: over any
 * window of `window_ms` ending at any instant, the number of admitted requests
 * is at most `limit`. No boundary effect, no estimate.
 *
 *     #include <policybook/rate_limiter/rate_limiter.h>
 *     #include <policybook/rate_limiter/sliding_log.h>
 *
 *     pb_ratelimiter_sliding_log_params params =
 *         PB_RATELIMITER_SLIDING_LOG_PARAMS_DEFAULT;
 *     pb_ratelimiter *limiter = pb_ratelimiter_sliding_log.create(&params, NULL, NULL);
 *     if (pb_ratelimiter_sliding_log.allow(limiter, key, 1u, now_ms)) { ... }
 *     pb_ratelimiter_sliding_log.destroy(limiter);
 *
 * The cost is memory, and in C it is visible in the signature rather than
 * hidden: the whole log is one allocation of `max_keys * limit` timestamps,
 * taken in `create`. At the default 100 permits over 1,024 keys that is 819,200
 * bytes. Check `memory_bytes` before choosing this policy on a constrained
 * target.
 *
 * **`max_keys` is a C-only parameter.** The TypeScript and Python ports grow a
 * hash map without limit; C takes all its memory in `create` and never
 * allocates again. Once the table is full a key that has
 * never been seen is refused — fail-closed, because the alternative is to stop
 * limiting the moment an attacker cycles through keys.
 *
 * Memory: 8 bytes per permit per tracked key, plus the map.
 */

#ifndef POLICYBOOK_RATE_LIMITER_SLIDING_LOG_H
#define POLICYBOOK_RATE_LIMITER_SLIDING_LOG_H

#include <stdint.h>

#include "policybook/rate_limiter/rate_limiter.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_ratelimiter_sliding_log_params {
    uint32_t limit;     /* requests allowed in any window of window_ms */
    uint32_t window_ms; /* window length, in milliseconds */
    uint32_t max_keys;  /* C only: keys tracked before new ones are refused */
} pb_ratelimiter_sliding_log_params;

#define PB_RATELIMITER_SLIDING_LOG_PARAMS_DEFAULT { 100u, 1000u, 1024u }

extern const pb_ratelimiter_vtable pb_ratelimiter_sliding_log;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_RATE_LIMITER_SLIDING_LOG_H */
