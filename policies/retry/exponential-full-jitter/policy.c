/*
 * ExponentialFullJitter — a uniform delay between zero and the exponential ceiling.
 *
 * Mirrors index.ts and policy.py. The ceiling is computed exactly as
 * `pb_retry_exponential` computes it, and restated here rather than shared for
 * the same reason the TypeScript and Python ports restate it: a policy file is
 * copied out of the registry whole, and a copy that reached into a sibling
 * policy would not build in the reader's project. The shared vectors pin the
 * two against each other.
 */

#include "policybook/retry/exponential_full_jitter.h"

#include <assert.h>
#include <stddef.h>

#include "policybook/allocator.h"

typedef struct pb_retry_exponential_full_jitter_state {
    const pb_allocator *allocator;
    pb_rng *rng;   /* borrowed from the caller */
    pb_rng owned;  /* used only when the caller supplied none */
    uint32_t base_ms;
    uint32_t cap_ms;
    uint32_t max_attempts;
} pb_retry_exponential_full_jitter_state;

/* min(cap, base * 2^(attempt - 1)), in integers and without overflowing. */
static uint32_t exponential_full_jitter_backoff_ceiling(uint32_t attempt, uint32_t base_ms, uint32_t cap_ms)
{
    /* 64-bit, as the float64 reference effectively is: with a cap above 2^31
     * the doubling that passes it would wrap a uint32 and dodge the clamp. */
    uint64_t delay = base_ms;
    uint32_t step;

    for (step = 1u; step < attempt; ++step) {
        if (delay >= cap_ms) {
            return cap_ms;
        }
        delay *= 2u;
    }
    return delay < cap_ms ? (uint32_t)delay : cap_ms;
}

static pb_retry *full_jitter_create(const void *params, const pb_allocator *allocator,
                                    pb_rng *rng)
{
    const pb_retry_exponential_full_jitter_params *config =
        (const pb_retry_exponential_full_jitter_params *)params;
    pb_retry_exponential_full_jitter_state *self;
    uint32_t base_ms;
    uint32_t cap_ms;
    uint32_t max_attempts;

    base_ms = (config == NULL) ? 100u : config->base_ms;
    cap_ms = (config == NULL) ? 10000u : config->cap_ms;
    max_attempts = (config == NULL) ? 8u : config->max_attempts;
    if (base_ms == 0u || cap_ms == 0u || max_attempts == 0u) {
        return NULL;
    }

    self = (pb_retry_exponential_full_jitter_state *)pb_alloc(
        allocator, sizeof(pb_retry_exponential_full_jitter_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->base_ms = base_ms;
    self->cap_ms = cap_ms;
    self->max_attempts = max_attempts;

    if (rng != NULL) {
        self->rng = rng;
    } else {
        /* Seeded rather than left absent: a policy constructed without one
         * still has to produce a delay, and an unseeded default would be a
         * global source by another name. */
        pb_rng_init(&self->owned, 0u);
        self->rng = &self->owned;
    }

    return (pb_retry *)self;
}

static void full_jitter_destroy(pb_retry *policy)
{
    pb_retry_exponential_full_jitter_state *self =
        (pb_retry_exponential_full_jitter_state *)policy;

    if (self == NULL) {
        return;
    }
    /* `rng` is borrowed when the caller supplied one, and points into this
     * struct otherwise. Either way there is nothing separate to free. */
    pb_free(self->allocator, self, sizeof(pb_retry_exponential_full_jitter_state));
}

static int64_t full_jitter_next_delay(pb_retry *policy, uint32_t attempt,
                                      const pb_retry_error *error)
{
    pb_retry_exponential_full_jitter_state *self =
        (pb_retry_exponential_full_jitter_state *)policy;
    uint32_t ceiling;

    assert(self != NULL);
    assert(error != NULL);

    /* Both refusals come before the draw, so a call this policy declines leaves
     * its random stream exactly where it was. A port that ordered these
     * differently would diverge from the first give-up onward. */
    if (!error->retryable) {
        return PB_RETRY_GIVE_UP;
    }
    if (attempt >= self->max_attempts) {
        return PB_RETRY_GIVE_UP;
    }

    /* `next_int(n)` returns 0..n-1, so the bound is the ceiling plus one and
     * the ceiling itself remains reachable. Zero is reachable too, and that is
     * deliberate: some client retrying immediately is what makes the arrival
     * pattern smooth rather than merely delayed.
     *
     * At a ceiling of UINT32_MAX the bound is 2^32: the reference's rejection
     * threshold is zero there and the draw is one raw 32-bit word, which is
     * what `next_u32` returns. `ceiling + 1u` would wrap the bound to zero. */
    ceiling = exponential_full_jitter_backoff_ceiling(attempt, self->base_ms, self->cap_ms);
    if (ceiling == UINT32_MAX) {
        return (int64_t)pb_rng_next_u32(self->rng);
    }
    return (int64_t)pb_rng_next_int(self->rng, ceiling + 1u);
}

static size_t full_jitter_memory_bytes(const pb_retry *policy)
{
    (void)policy;
    return sizeof(pb_retry_exponential_full_jitter_state);
}

const pb_retry_vtable pb_retry_exponential_full_jitter = { full_jitter_create,
                                                           full_jitter_next_delay,
                                                           full_jitter_memory_bytes,
                                                           full_jitter_destroy };
