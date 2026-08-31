/*
 * The `rate-limiter` domain: deciding whether a request may proceed.
 *
 * A limiter is asked one question — may this request go through, right now? —
 * and the interesting part is what it does with the ones it refuses. A fixed
 * window lets through twice its limit at a window boundary. A token bucket
 * absorbs bursts by design. A leaky bucket refuses to.
 *
 * Every policy exports a `const pb_ratelimiter_vtable` and a params struct with
 * a _DEFAULT initialiser, so a caller can swap policies at runtime by pointing
 * at a different vtable, or call one policy's functions directly:
 *
 *     #include <policybook/rate_limiter/rate_limiter.h>
 *     #include <policybook/rate_limiter/token_bucket.h>
 *
 *     pb_ratelimiter_token_bucket_params params =
 *         PB_RATELIMITER_TOKEN_BUCKET_PARAMS_DEFAULT;
 *     pb_ratelimiter *limiter = pb_ratelimiter_token_bucket.create(&params, NULL, NULL);
 *     if (pb_ratelimiter_token_bucket.allow(limiter, key, 1u, now_ms)) { ... }
 *     pb_ratelimiter_token_bucket.destroy(limiter);
 *
 * Keys are uint64_t: callers hash their own keys. **Time is an integer number of
 * milliseconds passed in by the caller, never read from a clock** — a policy
 * that reads the clock cannot be tested, and a caller with its own time source
 * could not use it. Every operation on that time is integer arithmetic too:
 * milli-token ledgers with an explicit carry rather than floating-point token
 * counts. Floats drift, and three languages
 * drift differently.
 */

#ifndef POLICYBOOK_RATE_LIMITER_RATE_LIMITER_H
#define POLICYBOOK_RATE_LIMITER_RATE_LIMITER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "policybook/allocator.h"
#include "policybook/rng.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque per-policy state. */
typedef struct pb_ratelimiter pb_ratelimiter;

/*
 * The reference configuration every canonical benchmark uses.
 *
 * Policies express their limits differently — permits per window, tokens per
 * second, an emission interval — so it is stated once in neutral terms and each
 * policy's README says how it maps onto its own parameters.
 */
#define PB_RATELIMITER_REFERENCE_PERMITS_PER_SECOND 100u
#define PB_RATELIMITER_REFERENCE_BURST 100u

/*
 * Returned by `retry_after` when the policy genuinely cannot say.
 *
 * In practice this means one thing: the key is not tracked and `max_keys` is
 * exhausted, so it will never be admitted and no finite wait is truthful. The
 * fuzzer treats any other use as a violation — a policy that returns this and
 * then admits the very next request was simply wrong.
 *
 * Returning 0 here instead would be a lie a caller acts on, and it is the bug
 * the rate-limiter fuzzer found on its first run.
 */
#define PB_RATELIMITER_RETRY_UNKNOWN UINT64_MAX

typedef struct pb_ratelimiter_vtable {
    /*
     * Allocate the policy and everything it will ever need.
     *
     * `params` points at the policy's own params struct. `allocator` may be
     * NULL for malloc/free. `rng` may be NULL for a policy that needs no
     * randomness.
     *
     * Returns NULL if allocation fails or the params are invalid.
     */
    pb_ratelimiter *(*create)(const void *params, const pb_allocator *allocator, pb_rng *rng);

    /*
     * May a request of `cost` units for `key` proceed at `now_ms`?
     *
     * `now_ms` is non-decreasing. Returning true is the decision *and* the
     * commitment: whatever budget the request consumes has been consumed by the
     * time this returns, so a policy must not be called speculatively.
     */
    bool (*allow)(pb_ratelimiter *limiter, uint64_t key, uint32_t cost, uint64_t now_ms);

    /*
     * How long until `key` could succeed, in milliseconds. A hint, not a
     * promise: it reflects only what the policy can prove from its own state at
     * `now_ms`, so a caller that waits exactly this long may still be refused
     * if others arrive meanwhile. Zero means "try now".
     *
     * May be NULL for a policy that does not implement it.
     */
    uint64_t (*retry_after)(pb_ratelimiter *limiter, uint64_t key, uint64_t now_ms);

    /*
     * How many keys the policy is currently tracking.
     *
     * Optional introspection for the memory metric: the number that separates a limiter you can run for a million keys
     * from one you cannot. May be NULL.
     */
    size_t (*state_size)(const pb_ratelimiter *limiter);

    /* Bytes held by the policy, for the memory column. May be NULL. */
    size_t (*memory_bytes)(const pb_ratelimiter *limiter);

    /* Release everything `create` took. Safe on NULL. */
    void (*destroy)(pb_ratelimiter *limiter);
} pb_ratelimiter_vtable;

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_RATE_LIMITER_RATE_LIMITER_H */
