/*
 * Exponential — double the wait after every failure, up to a cap.
 *
 * The textbook answer, and a genuine improvement on constant backoff: load on a
 * struggling service falls off geometrically as an outage continues.
 *
 * It still synchronises clients, and that is the reason not to ship it. The
 * delay is a pure function of the attempt number, so every client that failed
 * at the same moment retries at the same moment. Backing off exponentially
 * converts a continuous herd into a periodic one; it does not disperse it.
 * `pb_retry_exponential_full_jitter` is this plus one draw.
 *
 *     #include <policybook/retry/retry.h>
 *     #include <policybook/retry/exponential.h>
 *
 *     pb_retry_exponential_params params = PB_RETRY_EXPONENTIAL_PARAMS_DEFAULT;
 *     pb_retry_error error = PB_RETRY_ERROR_DEFAULT;
 *     pb_retry *policy = pb_retry_exponential.create(&params, NULL, NULL);
 *     int64_t delay = pb_retry_exponential.next_delay(policy, 3u, &error);
 *     pb_retry_exponential.destroy(policy);
 *
 * `rng` may be NULL: no draw anywhere, which is why it synchronises.
 *
 * Memory: the struct alone.
 */

#ifndef POLICYBOOK_RETRY_EXPONENTIAL_H
#define POLICYBOOK_RETRY_EXPONENTIAL_H

#include <stdint.h>

#include "policybook/retry/retry.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_retry_exponential_params {
    uint32_t base_ms;      /* the first delay, doubled on each attempt */
    uint32_t cap_ms;       /* no delay exceeds this */
    uint32_t max_attempts; /* give up after this many attempts */
} pb_retry_exponential_params;

#define PB_RETRY_EXPONENTIAL_PARAMS_DEFAULT { 100u, 10000u, 8u }

extern const pb_retry_vtable pb_retry_exponential;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_RETRY_EXPONENTIAL_H */
