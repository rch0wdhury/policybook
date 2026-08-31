/*
 * Constant — wait the same amount every time.
 *
 * The baseline, and the policy you get by accident when nobody thought about
 * it. It is the fastest to notice a *short* outage, and with no randomness
 * anywhere every client that failed together comes back together, forever.
 *
 *     #include <policybook/retry/retry.h>
 *     #include <policybook/retry/constant.h>
 *
 *     pb_retry_constant_params params = PB_RETRY_CONSTANT_PARAMS_DEFAULT;
 *     pb_retry_error error = PB_RETRY_ERROR_DEFAULT;
 *     pb_retry *policy = pb_retry_constant.create(&params, NULL, NULL);
 *     int64_t delay = pb_retry_constant.next_delay(policy, 1u, &error);
 *     pb_retry_constant.destroy(policy);
 *
 * `rng` may be NULL: this policy draws nothing, which is precisely its defining
 * weakness.
 *
 * Memory: the struct alone. Nothing per attempt and nothing per key.
 */

#ifndef POLICYBOOK_RETRY_CONSTANT_H
#define POLICYBOOK_RETRY_CONSTANT_H

#include <stdint.h>

#include "policybook/retry/retry.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_retry_constant_params {
    uint32_t base_ms;      /* the delay before every retry */
    uint32_t max_attempts; /* give up after this many attempts */
} pb_retry_constant_params;

#define PB_RETRY_CONSTANT_PARAMS_DEFAULT { 100u, 8u }

extern const pb_retry_vtable pb_retry_constant;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_RETRY_CONSTANT_H */
