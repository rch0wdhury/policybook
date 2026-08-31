/*
 * Exponential — double the wait after every failure, up to a cap.
 *
 * Mirrors index.ts and policy.py, including the shape of the ceiling
 * computation: it multiplies and stops at the cap rather than shifting, so the
 * arithmetic cannot run past the width of the integer however large `attempt`
 * is. `base << (attempt - 1)` would be undefined behaviour at attempt 33.
 */

#include "policybook/retry/exponential.h"

#include <assert.h>
#include <stddef.h>

#include "policybook/allocator.h"

typedef struct pb_retry_exponential_state {
    const pb_allocator *allocator;
    uint32_t base_ms;
    uint32_t cap_ms;
    uint32_t max_attempts;
} pb_retry_exponential_state;

/*
 * min(cap, base * 2^(attempt - 1)), in integers and without overflowing.
 *
 * The same function the jittered policy restates. Each language keeps its own
 * copy so that a policy file copied out of the registry compiles on its own;
 * the copies are pinned against each other by the shared vectors.
 */
static uint32_t exponential_backoff_ceiling(uint32_t attempt, uint32_t base_ms, uint32_t cap_ms)
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

static pb_retry *exponential_create(const void *params, const pb_allocator *allocator,
                                    pb_rng *rng)
{
    const pb_retry_exponential_params *config = (const pb_retry_exponential_params *)params;
    pb_retry_exponential_state *self;
    uint32_t base_ms;
    uint32_t cap_ms;
    uint32_t max_attempts;

    (void)rng; /* no draw anywhere, which is why it synchronises */

    base_ms = (config == NULL) ? 100u : config->base_ms;
    cap_ms = (config == NULL) ? 10000u : config->cap_ms;
    max_attempts = (config == NULL) ? 8u : config->max_attempts;
    if (base_ms == 0u || cap_ms == 0u || max_attempts == 0u) {
        return NULL;
    }

    self = (pb_retry_exponential_state *)pb_alloc(allocator,
                                                  sizeof(pb_retry_exponential_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->base_ms = base_ms;
    self->cap_ms = cap_ms;
    self->max_attempts = max_attempts;
    return (pb_retry *)self;
}

static void exponential_destroy(pb_retry *policy)
{
    pb_retry_exponential_state *self = (pb_retry_exponential_state *)policy;

    if (self == NULL) {
        return;
    }
    pb_free(self->allocator, self, sizeof(pb_retry_exponential_state));
}

static int64_t exponential_next_delay(pb_retry *policy, uint32_t attempt,
                                      const pb_retry_error *error)
{
    pb_retry_exponential_state *self = (pb_retry_exponential_state *)policy;

    assert(self != NULL);
    assert(error != NULL);

    if (!error->retryable) {
        return PB_RETRY_GIVE_UP;
    }
    if (attempt >= self->max_attempts) {
        return PB_RETRY_GIVE_UP;
    }
    return (int64_t)exponential_backoff_ceiling(attempt, self->base_ms, self->cap_ms);
}

static size_t exponential_memory_bytes(const pb_retry *policy)
{
    (void)policy;
    return sizeof(pb_retry_exponential_state);
}

const pb_retry_vtable pb_retry_exponential = { exponential_create, exponential_next_delay,
                                               exponential_memory_bytes, exponential_destroy };
