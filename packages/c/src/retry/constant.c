/*
 * GENERATED COPY — do not edit. Edit policies/retry/constant/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * Constant — wait the same amount every time.
 *
 * Mirrors index.ts and policy.py. Two integers and no state at all: asking
 * twice gives the same answer, which is exactly what synchronises a fleet.
 */

#include "policybook/retry/constant.h"

#include <assert.h>
#include <stddef.h>

#include "policybook/allocator.h"

typedef struct pb_retry_constant_state {
    const pb_allocator *allocator;
    uint32_t base_ms;
    uint32_t max_attempts;
} pb_retry_constant_state;

static pb_retry *constant_create(const void *params, const pb_allocator *allocator, pb_rng *rng)
{
    const pb_retry_constant_params *config = (const pb_retry_constant_params *)params;
    pb_retry_constant_state *self;
    uint32_t base_ms;
    uint32_t max_attempts;

    (void)rng; /* draws nothing, which is the whole problem with it */

    base_ms = (config == NULL) ? 100u : config->base_ms;
    max_attempts = (config == NULL) ? 8u : config->max_attempts;
    /* base_ms of zero is legitimate — retrying immediately is aggressive, not
     * invalid — so only the attempt budget has a floor. */
    if (max_attempts == 0u) {
        return NULL;
    }

    self = (pb_retry_constant_state *)pb_alloc(allocator, sizeof(pb_retry_constant_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->base_ms = base_ms;
    self->max_attempts = max_attempts;
    return (pb_retry *)self;
}

static void constant_destroy(pb_retry *policy)
{
    pb_retry_constant_state *self = (pb_retry_constant_state *)policy;

    if (self == NULL) {
        return;
    }
    pb_free(self->allocator, self, sizeof(pb_retry_constant_state));
}

static int64_t constant_next_delay(pb_retry *policy, uint32_t attempt,
                                   const pb_retry_error *error)
{
    pb_retry_constant_state *self = (pb_retry_constant_state *)policy;

    assert(self != NULL);
    assert(error != NULL);

    /* Nothing is gained by retrying a failure the server says is permanent. */
    if (!error->retryable) {
        return PB_RETRY_GIVE_UP;
    }
    if (attempt >= self->max_attempts) {
        return PB_RETRY_GIVE_UP;
    }
    return (int64_t)self->base_ms;
}

static size_t constant_memory_bytes(const pb_retry *policy)
{
    (void)policy;
    return sizeof(pb_retry_constant_state);
}

const pb_retry_vtable pb_retry_constant = { constant_create, constant_next_delay,
                                            constant_memory_bytes, constant_destroy };
