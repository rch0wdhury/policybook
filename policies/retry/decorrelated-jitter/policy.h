/*
 * DecorrelatedJitter — grow the delay from the last delay, not the attempt number.
 *
 * The other policies in this domain compute their delay from `attempt`, so a
 * client's whole schedule is determined the moment it starts failing. This one
 * is a random walk instead:
 *
 *     delay = min(cap, base + next_int(prev * 3 - base + 1))
 *     prev  = delay
 *
 * `prev` is state, and it is the whole idea: the delay depends on the history
 * of the retry sequence rather than on its length, so whole schedules diverge
 * rather than individual attempts. This is the only policy in the domain that
 * remembers anything between calls.
 *
 *     #include <policybook/retry/retry.h>
 *     #include <policybook/retry/decorrelated_jitter.h>
 *
 *     pb_rng rng;
 *     pb_retry_decorrelated_jitter_params params =
 *         PB_RETRY_DECORRELATED_JITTER_PARAMS_DEFAULT;
 *     pb_retry_error error = PB_RETRY_ERROR_DEFAULT;
 *
 *     pb_rng_init(&rng, 7u);
 *     pb_retry *policy = pb_retry_decorrelated_jitter.create(&params, NULL, &rng);
 *     int64_t delay = pb_retry_decorrelated_jitter.next_delay(policy, 1u, &error);
 *     pb_retry_decorrelated_jitter.destroy(policy);
 *
 * Because the walk lives in the policy object, its lifetime *is* the retry
 * sequence: a policy created per attempt restarts at `base` every time and
 * degenerates into a fixed-range draw.
 *
 * It climbs more slowly than doubling despite reaching for three times the last
 * delay — the draw is uniform over that range, so the expected step is about
 * 1.5x against exponential's exact 2x. The variance is the point, not the speed.
 *
 * Memory: the struct alone, holding one integer of walk state.
 */

#ifndef POLICYBOOK_RETRY_DECORRELATED_JITTER_H
#define POLICYBOOK_RETRY_DECORRELATED_JITTER_H

#include <stdint.h>

#include "policybook/retry/retry.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_retry_decorrelated_jitter_params {
    uint32_t base_ms;      /* the floor of every delay, and where the walk starts */
    uint32_t cap_ms;       /* no delay exceeds this */
    uint32_t max_attempts; /* give up after this many attempts */
} pb_retry_decorrelated_jitter_params;

#define PB_RETRY_DECORRELATED_JITTER_PARAMS_DEFAULT { 100u, 10000u, 8u }

extern const pb_retry_vtable pb_retry_decorrelated_jitter;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_RETRY_DECORRELATED_JITTER_H */
