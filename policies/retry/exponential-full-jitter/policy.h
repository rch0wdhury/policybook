/*
 * ExponentialFullJitter — a uniform delay between zero and the exponential ceiling.
 *
 * The domain's recommended default. It keeps everything exponential backoff
 * gets right — load falls geometrically as an outage continues — and fixes the
 * thing it gets wrong: clients no longer retry in lockstep, because each picks
 * its own delay.
 *
 *     #include <policybook/retry/retry.h>
 *     #include <policybook/retry/exponential_full_jitter.h>
 *
 *     pb_rng rng;
 *     pb_retry_exponential_full_jitter_params params =
 *         PB_RETRY_EXPONENTIAL_FULL_JITTER_PARAMS_DEFAULT;
 *     pb_retry_error error = PB_RETRY_ERROR_DEFAULT;
 *
 *     pb_rng_init(&rng, 7u);
 *     pb_retry *policy = pb_retry_exponential_full_jitter.create(&params, NULL, &rng);
 *     int64_t delay = pb_retry_exponential_full_jitter.next_delay(policy, 1u, &error);
 *     pb_retry_exponential_full_jitter.destroy(policy);
 *
 * The expected delay is half the un-jittered ceiling, so this is *more*
 * aggressive than plain exponential rather than less. It wins anyway, because
 * spreading a fleet matters more than the average wait.
 *
 * The `pb_rng` is borrowed, not owned: the caller keeps it alive for the
 * policy's lifetime. Passing NULL seeds a deterministic stream rather than
 * reaching for a global source.
 *
 * Memory: the struct alone.
 */

#ifndef POLICYBOOK_RETRY_EXPONENTIAL_FULL_JITTER_H
#define POLICYBOOK_RETRY_EXPONENTIAL_FULL_JITTER_H

#include <stdint.h>

#include "policybook/retry/retry.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_retry_exponential_full_jitter_params {
    uint32_t base_ms;      /* the first ceiling, doubled on each attempt */
    uint32_t cap_ms;       /* no ceiling exceeds this */
    uint32_t max_attempts; /* give up after this many attempts */
} pb_retry_exponential_full_jitter_params;

#define PB_RETRY_EXPONENTIAL_FULL_JITTER_PARAMS_DEFAULT { 100u, 10000u, 8u }

extern const pb_retry_vtable pb_retry_exponential_full_jitter;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_RETRY_EXPONENTIAL_FULL_JITTER_H */
