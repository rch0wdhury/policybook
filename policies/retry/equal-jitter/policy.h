/*
 * EqualJitter — half the exponential delay fixed, half of it random.
 *
 * The middle ground between `pb_retry_exponential`, which spreads nothing, and
 * `pb_retry_exponential_full_jitter`, which spreads everything and halves the
 * expected wait in the process:
 *
 *     half  = min(cap, base * 2^(attempt-1)) / 2
 *     delay = half + next_int(half + 1)
 *
 * A delay always lands in [half, 2 x half] and averages about three quarters of
 * the un-jittered ceiling. Choose it when the backoff must actually back off:
 * full jitter can return zero on any attempt, and this cannot fall below half
 * the ceiling.
 *
 *     #include <policybook/retry/retry.h>
 *     #include <policybook/retry/equal_jitter.h>
 *
 *     pb_rng rng;
 *     pb_retry_equal_jitter_params params = PB_RETRY_EQUAL_JITTER_PARAMS_DEFAULT;
 *     pb_retry_error error = PB_RETRY_ERROR_DEFAULT;
 *
 *     pb_rng_init(&rng, 7u);
 *     pb_retry *policy = pb_retry_equal_jitter.create(&params, NULL, &rng);
 *     int64_t delay = pb_retry_equal_jitter.next_delay(policy, 1u, &error);
 *     pb_retry_equal_jitter.destroy(policy);
 *
 * The halving is integer division: at a ceiling of 1 the half is 0 and every
 * delay is 0. Degenerate, and documented rather than special-cased, because a
 * special case would be a different policy.
 *
 * Memory: the struct alone.
 */

#ifndef POLICYBOOK_RETRY_EQUAL_JITTER_H
#define POLICYBOOK_RETRY_EQUAL_JITTER_H

#include <stdint.h>

#include "policybook/retry/retry.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_retry_equal_jitter_params {
    uint32_t base_ms;      /* the first ceiling, doubled on each attempt */
    uint32_t cap_ms;       /* no ceiling exceeds this */
    uint32_t max_attempts; /* give up after this many attempts */
} pb_retry_equal_jitter_params;

#define PB_RETRY_EQUAL_JITTER_PARAMS_DEFAULT { 100u, 10000u, 8u }

extern const pb_retry_vtable pb_retry_equal_jitter;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_RETRY_EQUAL_JITTER_H */
