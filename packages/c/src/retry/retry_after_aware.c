/*
 * GENERATED COPY — do not edit. Edit policies/retry/retry-after-aware/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * RetryAfterAware — do what the server asked, and guess only when it did not.
 *
 * Mirrors index.ts and policy.py. The ceiling is restated here rather than
 * shared, for the same reason the other ports restate it: a policy file is
 * copied out of the registry whole.
 */

#include "policybook/retry/retry_after_aware.h"

#include <assert.h>
#include <stddef.h>

#include "policybook/allocator.h"

typedef struct pb_retry_retry_after_aware_state {
    const pb_allocator *allocator;
    pb_rng *rng;  /* borrowed from the caller */
    pb_rng owned; /* used only when the caller supplied none */
    uint32_t base_ms;
    uint32_t cap_ms;
    uint32_t max_attempts;
} pb_retry_retry_after_aware_state;

/* min(cap, base * 2^(attempt - 1)), in integers and without overflowing. */
static uint32_t retry_after_aware_backoff_ceiling(uint32_t attempt, uint32_t base_ms, uint32_t cap_ms)
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

static pb_retry *aware_create(const void *params, const pb_allocator *allocator, pb_rng *rng)
{
    const pb_retry_retry_after_aware_params *config =
        (const pb_retry_retry_after_aware_params *)params;
    pb_retry_retry_after_aware_state *self;
    uint32_t base_ms;
    uint32_t cap_ms;
    uint32_t max_attempts;

    base_ms = (config == NULL) ? 100u : config->base_ms;
    cap_ms = (config == NULL) ? 10000u : config->cap_ms;
    max_attempts = (config == NULL) ? 8u : config->max_attempts;
    if (base_ms == 0u || cap_ms == 0u || max_attempts == 0u) {
        return NULL;
    }

    self = (pb_retry_retry_after_aware_state *)pb_alloc(
        allocator, sizeof(pb_retry_retry_after_aware_state));
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
        pb_rng_init(&self->owned, 0u);
        self->rng = &self->owned;
    }
    return (pb_retry *)self;
}

static void aware_destroy(pb_retry *policy)
{
    pb_retry_retry_after_aware_state *self = (pb_retry_retry_after_aware_state *)policy;

    if (self == NULL) {
        return;
    }
    pb_free(self->allocator, self, sizeof(pb_retry_retry_after_aware_state));
}

static int64_t aware_next_delay(pb_retry *policy, uint32_t attempt,
                                const pb_retry_error *error)
{
    pb_retry_retry_after_aware_state *self = (pb_retry_retry_after_aware_state *)policy;
    uint64_t hint;
    uint32_t ceiling;

    assert(self != NULL);
    assert(error != NULL);

    if (!error->retryable) {
        return PB_RETRY_GIVE_UP;
    }
    if (attempt >= self->max_attempts) {
        return PB_RETRY_GIVE_UP;
    }

    if (error->has_retry_after) {
        /* No draw is consumed on this path. A port that drew anyway would leave
         * its stream in a different place and diverge on the next fallback. */
        hint = error->retry_after_ms;
        if (hint > (uint64_t)self->cap_ms) {
            hint = (uint64_t)self->cap_ms;
        }
        return (int64_t)hint;
    }

    /* The bound is the ceiling plus one. At a ceiling of UINT32_MAX that is
     * 2^32: the reference's rejection threshold is zero there and the draw is
     * one raw 32-bit word, which is what `next_u32` returns. `ceiling + 1u`
     * would wrap the bound to zero. */
    ceiling = retry_after_aware_backoff_ceiling(attempt, self->base_ms, self->cap_ms);
    if (ceiling == UINT32_MAX) {
        return (int64_t)pb_rng_next_u32(self->rng);
    }
    return (int64_t)pb_rng_next_int(self->rng, ceiling + 1u);
}

static size_t aware_memory_bytes(const pb_retry *policy)
{
    (void)policy;
    return sizeof(pb_retry_retry_after_aware_state);
}

const pb_retry_vtable pb_retry_retry_after_aware = { aware_create, aware_next_delay,
                                                     aware_memory_bytes, aware_destroy };
