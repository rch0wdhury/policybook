/*
 * DecorrelatedJitter — grow the delay from the last delay, not the attempt number.
 *
 * Mirrors index.ts and policy.py. The only stateful policy in the domain: the
 * walk lives in `previous_ms`, and the object's lifetime is the retry sequence.
 */

#include "policybook/retry/decorrelated_jitter.h"

#include <assert.h>
#include <stddef.h>

#include "policybook/allocator.h"

typedef struct pb_retry_decorrelated_jitter_state {
    const pb_allocator *allocator;
    pb_rng *rng;  /* borrowed from the caller */
    pb_rng owned; /* used only when the caller supplied none */
    uint32_t base_ms;
    uint32_t cap_ms;
    uint32_t max_attempts;
    uint32_t previous_ms; /* the walk; starts at base_ms */
} pb_retry_decorrelated_jitter_state;

static pb_retry *decorrelated_create(const void *params, const pb_allocator *allocator,
                                     pb_rng *rng)
{
    const pb_retry_decorrelated_jitter_params *config =
        (const pb_retry_decorrelated_jitter_params *)params;
    pb_retry_decorrelated_jitter_state *self;
    uint32_t base_ms;
    uint32_t cap_ms;
    uint32_t max_attempts;

    base_ms = (config == NULL) ? 100u : config->base_ms;
    cap_ms = (config == NULL) ? 10000u : config->cap_ms;
    max_attempts = (config == NULL) ? 8u : config->max_attempts;
    if (base_ms == 0u || cap_ms == 0u || max_attempts == 0u) {
        return NULL;
    }
    /* `prev * 3` must stay inside 32 bits. `prev` never exceeds `cap_ms`, so
     * bounding the cap bounds the whole walk. */
    if (cap_ms > UINT32_MAX / 3u) {
        return NULL;
    }

    self = (pb_retry_decorrelated_jitter_state *)pb_alloc(
        allocator, sizeof(pb_retry_decorrelated_jitter_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->base_ms = base_ms;
    self->cap_ms = cap_ms;
    self->max_attempts = max_attempts;
    self->previous_ms = base_ms;

    if (rng != NULL) {
        self->rng = rng;
    } else {
        pb_rng_init(&self->owned, 0u);
        self->rng = &self->owned;
    }
    return (pb_retry *)self;
}

static void decorrelated_destroy(pb_retry *policy)
{
    pb_retry_decorrelated_jitter_state *self = (pb_retry_decorrelated_jitter_state *)policy;

    if (self == NULL) {
        return;
    }
    pb_free(self->allocator, self, sizeof(pb_retry_decorrelated_jitter_state));
}

static int64_t decorrelated_next_delay(pb_retry *policy, uint32_t attempt,
                                       const pb_retry_error *error)
{
    pb_retry_decorrelated_jitter_state *self = (pb_retry_decorrelated_jitter_state *)policy;
    uint32_t span;
    uint32_t delay;

    assert(self != NULL);
    assert(error != NULL);

    /* Both refusals precede the draw, so a declined call leaves neither the
     * stream nor the walk advanced. */
    if (!error->retryable) {
        return PB_RETRY_GIVE_UP;
    }
    if (attempt >= self->max_attempts) {
        return PB_RETRY_GIVE_UP;
    }

    /* `prev * 3 - base` is always positive: `prev` starts at `base` and every
     * delay is at least `base`, so the smallest span is `2 * base`. */
    span = self->previous_ms * 3u - self->base_ms;
    delay = self->base_ms + pb_rng_next_int(self->rng, span + 1u);
    if (delay > self->cap_ms) {
        delay = self->cap_ms;
    }

    /* The walk advances from the delay actually used, cap included — otherwise
     * a client at the cap would keep drawing from an ever-growing range it can
     * never reach. */
    self->previous_ms = delay;
    return (int64_t)delay;
}

static size_t decorrelated_memory_bytes(const pb_retry *policy)
{
    (void)policy;
    return sizeof(pb_retry_decorrelated_jitter_state);
}

const pb_retry_vtable pb_retry_decorrelated_jitter = { decorrelated_create,
                                                       decorrelated_next_delay,
                                                       decorrelated_memory_bytes,
                                                       decorrelated_destroy };
