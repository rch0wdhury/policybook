/*
 * GENERATED COPY — do not edit. Edit policies/rate-limiter/sliding-counter/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * SlidingCounter — two fixed windows, weighted by how far into the new one you are.
 *
 * The practical compromise between a fixed window and a sliding log. Keep the
 * count for the current window and the previous one, then fade the old count
 * out as the new window fills:
 *
 *     estimate = previous * (window_ms - elapsed) / window_ms + current
 *
 * Three integers per key instead of a hundred timestamps, and the boundary
 * burst is gone.
 *
 *     #include <policybook/rate_limiter/rate_limiter.h>
 *     #include <policybook/rate_limiter/sliding_counter.h>
 *
 *     pb_ratelimiter_sliding_counter_params params =
 *         PB_RATELIMITER_SLIDING_COUNTER_PARAMS_DEFAULT;
 *     pb_ratelimiter *limiter = pb_ratelimiter_sliding_counter.create(&params, NULL, NULL);
 *     if (pb_ratelimiter_sliding_counter.allow(limiter, key, 1u, now_ms)) { ... }
 *     pb_ratelimiter_sliding_counter.destroy(limiter);
 *
 * The weighting is integer arithmetic with the remainder discarded, matching
 * the TypeScript and Python ports exactly. A
 * floating-point version of this line would eventually disagree with them.
 *
 * **`max_keys` is a C-only parameter.** The TypeScript and Python ports grow a
 * hash map without limit; C takes all its memory in `create` and never
 * allocates again (concept.md §12.2). Once the table is full a key that has
 * never been seen is refused — fail-closed, because the alternative is to stop
 * limiting the moment an attacker cycles through keys.
 *
 * Memory: 16 bytes per tracked key, plus the map.
 */

#ifndef POLICYBOOK_RATE_LIMITER_SLIDING_COUNTER_H
#define POLICYBOOK_RATE_LIMITER_SLIDING_COUNTER_H

#include <stdint.h>

#include "policybook/rate_limiter/rate_limiter.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_ratelimiter_sliding_counter_params {
    uint32_t limit;     /* requests allowed per window */
    uint32_t window_ms; /* window length, in milliseconds */
    uint32_t max_keys;  /* C only: keys tracked before new ones are refused */
} pb_ratelimiter_sliding_counter_params;

#define PB_RATELIMITER_SLIDING_COUNTER_PARAMS_DEFAULT { 100u, 1000u, 1024u }

extern const pb_ratelimiter_vtable pb_ratelimiter_sliding_counter;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_RATE_LIMITER_SLIDING_COUNTER_H */
