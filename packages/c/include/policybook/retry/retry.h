/*
 * The `retry` domain: how long to wait before trying again.
 *
 * The smallest decision in the registry and the one most often got wrong. A
 * request failed; when should the client come back? Retry too eagerly and a
 * service that was merely slow becomes a service that is down, because every
 * client in the fleet is now hammering it in lockstep (concept.md §5.1).
 *
 * Every policy exports a `const pb_retry_vtable` and a params struct with a
 * _DEFAULT initialiser:
 *
 *     #include <policybook/retry/retry.h>
 *     #include <policybook/retry/exponential_full_jitter.h>
 *
 *     pb_rng rng;
 *     pb_retry_error error = PB_RETRY_ERROR_DEFAULT;
 *     pb_retry_exponential_full_jitter_params params =
 *         PB_RETRY_EXPONENTIAL_FULL_JITTER_PARAMS_DEFAULT;
 *
 *     pb_rng_init(&rng, 7u);
 *     pb_retry *policy = pb_retry_exponential_full_jitter.create(&params, NULL, &rng);
 *     int64_t delay = pb_retry_exponential_full_jitter.next_delay(policy, 1u, &error);
 *     if (delay == PB_RETRY_GIVE_UP) { ... }
 *     pb_retry_exponential_full_jitter.destroy(policy);
 *
 * The `pb_rng` is supplied at `create`, as it is for every other domain here.
 * concept.md §5.1 threads it through the per-call function instead; putting it
 * on the hot path would buy nothing, and the property that matters — a policy
 * never reaching for a global source — is unchanged.
 */

#ifndef POLICYBOOK_RETRY_RETRY_H
#define POLICYBOOK_RETRY_RETRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "policybook/allocator.h"
#include "policybook/rng.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque per-policy state. */
typedef struct pb_retry pb_retry;

/*
 * Returned by `next_delay` when the policy has decided to stop.
 *
 * A decision, not an error. Negative so it can never collide with a delay,
 * which is why the function returns a signed type.
 */
#define PB_RETRY_GIVE_UP ((int64_t)-1)

/* What went wrong, as much of it as a policy is allowed to know. */
typedef struct pb_retry_error {
    int32_t status;  /* HTTP-style status, or 0 when there is none */
    bool retryable;  /* whether retrying could plausibly help at all */
    /*
     * How long the server asked the client to wait, in milliseconds.
     *
     * `has_retry_after` says whether the server said anything at all; most
     * errors carry no `Retry-After`, and "zero" is a different statement from
     * "nothing". A policy that ignores a server's own estimate is guessing when
     * it has been told.
     */
    bool has_retry_after;
    uint64_t retry_after_ms;
} pb_retry_error;

/* A retryable 503 carrying no Retry-After — the common case. */
#define PB_RETRY_ERROR_DEFAULT { 503, true, false, 0u }

typedef struct pb_retry_vtable {
    /*
     * Allocate the policy and everything it will ever need.
     *
     * `rng` may be NULL for a policy that draws nothing; a jittered policy
     * given NULL seeds itself deterministically rather than reaching for a
     * global source.
     */
    pb_retry *(*create)(const void *params, const pb_allocator *allocator, pb_rng *rng);

    /*
     * How long to wait before attempt `attempt + 1`, or PB_RETRY_GIVE_UP.
     *
     * `attempt` is 1-based: it is the number of the attempt that just failed,
     * so the first call always has `attempt == 1`.
     */
    int64_t (*next_delay)(pb_retry *policy, uint32_t attempt, const pb_retry_error *error);

    /* Bytes held by the policy, for the memory column. May be NULL. */
    size_t (*memory_bytes)(const pb_retry *policy);

    /* Release everything `create` took. Safe on NULL. */
    void (*destroy)(pb_retry *policy);
} pb_retry_vtable;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_RETRY_RETRY_H */
