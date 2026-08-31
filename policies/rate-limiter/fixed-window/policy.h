/*
 * FixedWindow — count requests inside a clock-aligned window, reset at the edge.
 *
 * The simplest limiter that works. Two integers per key, no allocation on the
 * hot path, and one well-known flaw: a client can send `limit` requests at the
 * end of one window and `limit` more at the start of the next, putting
 * 2 x limit through in an interval shorter than a single window.
 *
 *     #include <policybook/rate_limiter/rate_limiter.h>
 *     #include <policybook/rate_limiter/fixed_window.h>
 *
 *     pb_ratelimiter_fixed_window_params params =
 *         PB_RATELIMITER_FIXED_WINDOW_PARAMS_DEFAULT;
 *     params.limit = 100u;
 *     pb_ratelimiter *limiter = pb_ratelimiter_fixed_window.create(&params, NULL, NULL);
 *     if (pb_ratelimiter_fixed_window.allow(limiter, key, 1u, now_ms)) { ... }
 *     pb_ratelimiter_fixed_window.destroy(limiter);
 *
 * Windows are aligned to the epoch, not to a key's first request, which is what
 * lets separate processes agree on the current window without coordinating.
 *
 * **`max_keys` is a C-only parameter.** The TypeScript and Python ports grow a
 * hash map without limit; C takes all its memory in `create` and never
 * allocates again, so the number of tracked keys has to be
 * bounded up front. Once the table is full, a key that has never been seen is
 * **refused** — fail-closed, because the alternative is to stop limiting the
 * moment an attacker cycles through keys. Size it above the cardinality you
 * expect.
 *
 * Memory: 16 bytes per tracked key (a 64-bit window start and a 32-bit count,
 * padded), plus the map.
 */

#ifndef POLICYBOOK_RATE_LIMITER_FIXED_WINDOW_H
#define POLICYBOOK_RATE_LIMITER_FIXED_WINDOW_H

#include <stdint.h>

#include "policybook/rate_limiter/rate_limiter.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_ratelimiter_fixed_window_params {
    uint32_t limit;     /* requests allowed per window */
    uint32_t window_ms; /* window length, in milliseconds */
    uint32_t max_keys;  /* C only: keys tracked before new ones are refused */
} pb_ratelimiter_fixed_window_params;

#define PB_RATELIMITER_FIXED_WINDOW_PARAMS_DEFAULT { 100u, 1000u, 1024u }

extern const pb_ratelimiter_vtable pb_ratelimiter_fixed_window;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_RATE_LIMITER_FIXED_WINDOW_H */
