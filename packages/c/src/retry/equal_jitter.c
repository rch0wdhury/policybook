/*
 * GENERATED COPY — do not edit. Edit policies/retry/equal-jitter/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * EqualJitter — half the exponential delay fixed, half of it random.
 *
 * Mirrors index.ts and policy.py. The ceiling is restated here rather than
 * shared, for the same reason the other ports restate it: a policy file is
 * copied out of the registry whole, and a copy that reached into a sibling
 * policy would not build in the reader's project.
 */

#include "policybook/retry/equal_jitter.h"

#include <assert.h>
#include <stddef.h>

#include "policybook/allocator.h"

typedef struct pb_retry_equal_jitter_state {
    const pb_allocator *allocator;
    pb_rng *rng;  /* borrowed from the caller */
    pb_rng owned; /* used only when the caller supplied none */
    uint32_t base_ms;
    uint32_t cap_ms;
    uint32_t max_attempts;
} pb_retry_equal_jitter_state;

/* min(cap, base * 2^(attempt - 1)), in integers and without overflowing. */
static uint32_t equal_jitter_backoff_ceiling(uint32_t attempt, uint32_t base_ms, uint32_t cap_ms)
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

static pb_retry *equal_jitter_create(const void *params, const pb_allocator *allocator,
                                     pb_rng *rng)
{
    const pb_retry_equal_jitter_params *config =
        (const pb_retry_equal_jitter_params *)params;
    pb_retry_equal_jitter_state *self;
    uint32_t base_ms;
    uint32_t cap_ms;
    uint32_t max_attempts;

    base_ms = (config == NULL) ? 100u : config->base_ms;
    cap_ms = (config == NULL) ? 10000u : config->cap_ms;
    max_attempts = (config == NULL) ? 8u : config->max_attempts;
    if (base_ms == 0u || cap_ms == 0u || max_attempts == 0u) {
        return NULL;
    }

    self = (pb_retry_equal_jitter_state *)pb_alloc(allocator,
                                                   sizeof(pb_retry_equal_jitter_state));
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

static void equal_jitter_destroy(pb_retry *policy)
{
    pb_retry_equal_jitter_state *self = (pb_retry_equal_jitter_state *)policy;

    if (self == NULL) {
        return;
    }
    pb_free(self->allocator, self, sizeof(pb_retry_equal_jitter_state));
}

static int64_t equal_jitter_next_delay(pb_retry *policy, uint32_t attempt,
                                       const pb_retry_error *error)
{
    pb_retry_equal_jitter_state *self = (pb_retry_equal_jitter_state *)policy;
    uint32_t half;

    assert(self != NULL);
    assert(error != NULL);

    /* Both refusals precede the draw, so a declined call leaves the stream
     * exactly where it was. */
    if (!error->retryable) {
        return PB_RETRY_GIVE_UP;
    }
    if (attempt >= self->max_attempts) {
        return PB_RETRY_GIVE_UP;
    }

    /* Integer halving. At a ceiling of 1 the half is 0 and every delay is 0. */
    half = equal_jitter_backoff_ceiling(attempt, self->base_ms, self->cap_ms) / 2u;
    return (int64_t)half + (int64_t)pb_rng_next_int(self->rng, half + 1u);
}

static size_t equal_jitter_memory_bytes(const pb_retry *policy)
{
    (void)policy;
    return sizeof(pb_retry_equal_jitter_state);
}

const pb_retry_vtable pb_retry_equal_jitter = { equal_jitter_create, equal_jitter_next_delay,
                                                equal_jitter_memory_bytes,
                                                equal_jitter_destroy };
