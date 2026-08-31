/*
 * GENERATED COPY — do not edit. Edit policies/retry/retry-after-aware/policy.h instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * RetryAfterAware — do what the server asked, and guess only when it did not.
 *
 * Every other policy in this domain is guessing. This one reads the answer when
 * the server has provided it, and falls back to full jitter when it has not:
 *
 *     if error->has_retry_after:  delay = min(cap, error->retry_after_ms)
 *     otherwise:                  delay = next_int(ceiling + 1)
 *
 *     #include <policybook/retry/retry.h>
 *     #include <policybook/retry/retry_after_aware.h>
 *
 *     pb_rng rng;
 *     pb_retry_retry_after_aware_params params =
 *         PB_RETRY_RETRY_AFTER_AWARE_PARAMS_DEFAULT;
 *     pb_retry_error error = PB_RETRY_ERROR_DEFAULT;
 *
 *     error.has_retry_after = true;
 *     error.retry_after_ms = 2500u;
 *     pb_rng_init(&rng, 7u);
 *     pb_retry *policy = pb_retry_retry_after_aware.create(&params, NULL, &rng);
 *     int64_t delay = pb_retry_retry_after_aware.next_delay(policy, 1u, &error);
 *     pb_retry_retry_after_aware.destroy(policy);
 *
 * `has_retry_after` is what distinguishes "the server said nothing" from "the
 * server said zero". Zero is an instruction — come back now — and is honoured.
 *
 * **The clamp is not a formality.** A server under load can ask for minutes,
 * and a client that honours an arbitrary hint has handed a stranger control of
 * its own latency budget. `cap_ms` is the caller's statement of how long it is
 * willing to be told to wait.
 *
 * **This policy re-synchronises clients by construction.** A thousand told to
 * come back in five seconds all come back in five seconds — precisely the herd
 * jitter exists to break. See README.md for when that trade is worth making.
 *
 * Memory: the struct alone.
 */

#ifndef POLICYBOOK_RETRY_RETRY_AFTER_AWARE_H
#define POLICYBOOK_RETRY_RETRY_AFTER_AWARE_H

#include <stdint.h>

#include "policybook/retry/retry.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_retry_retry_after_aware_params {
    uint32_t base_ms;      /* the first fallback ceiling, doubled each attempt */
    uint32_t cap_ms;       /* the longest wait this client accepts, from any source */
    uint32_t max_attempts; /* give up after this many attempts */
} pb_retry_retry_after_aware_params;

#define PB_RETRY_RETRY_AFTER_AWARE_PARAMS_DEFAULT { 100u, 10000u, 8u }

extern const pb_retry_vtable pb_retry_retry_after_aware;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_RETRY_RETRY_AFTER_AWARE_H */
