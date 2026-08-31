/*
 * GENERATED COPY — do not edit. Edit policies/rate-limiter/fixed-window/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * FixedWindow — count requests inside a clock-aligned window, reset at the edge.
 *
 * Mirrors index.ts and policy.py. A map from key to slot, and two parallel
 * arrays holding the window each slot's count belongs to and the count itself.
 */

#include "policybook/rate_limiter/fixed_window.h"

#include <assert.h>
#include <stddef.h>

#include "policybook/allocator.h"
#include "policybook/ds/map.h"

typedef struct pb_ratelimiter_fixed_window_state {
    const pb_allocator *allocator;
    pb_map index; /* key -> slot */
    uint64_t *window_start;
    uint32_t *count;
    uint32_t limit;
    uint32_t window_ms;
    uint32_t max_keys;
    uint32_t used;
} pb_ratelimiter_fixed_window_state;

/* The start of the window containing `now`. Integer division, no floats. */
static uint64_t fixed_window_window_of(const pb_ratelimiter_fixed_window_state *self, uint64_t now)
{
    return now - (now % (uint64_t)self->window_ms);
}

/*
 * Find a key's slot, claiming a free one if it has never been seen.
 *
 * Returns false when the table is full, which is the fail-closed case: a new
 * key is refused rather than being silently let through.
 */
static bool fixed_window_slot_for(pb_ratelimiter_fixed_window_state *self, uint64_t key, uint32_t *slot)
{
    if (pb_map_get(&self->index, key, slot)) {
        return true;
    }
    if (self->used >= self->max_keys) {
        return false;
    }
    *slot = self->used;
    if (!pb_map_put(&self->index, key, *slot)) {
        return false;
    }
    self->used += 1u;
    self->window_start[*slot] = 0u;
    self->count[*slot] = 0u;
    return true;
}

static pb_ratelimiter *fixed_window_create(const void *params, const pb_allocator *allocator,
                                           pb_rng *rng)
{
    const pb_ratelimiter_fixed_window_params *config =
        (const pb_ratelimiter_fixed_window_params *)params;
    pb_ratelimiter_fixed_window_state *self;
    uint32_t limit;
    uint32_t window_ms;
    uint32_t max_keys;

    (void)rng; /* a fixed window makes no random choices */

    limit = (config == NULL) ? 100u : config->limit;
    window_ms = (config == NULL) ? 1000u : config->window_ms;
    max_keys = (config == NULL) ? 1024u : config->max_keys;
    if (limit == 0u || window_ms == 0u || max_keys == 0u) {
        return NULL;
    }

    self = (pb_ratelimiter_fixed_window_state *)pb_alloc(
        allocator, sizeof(pb_ratelimiter_fixed_window_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->limit = limit;
    self->window_ms = window_ms;
    self->max_keys = max_keys;
    self->used = 0u;
    self->window_start = NULL;
    self->count = NULL;

    if (!pb_map_init(&self->index, max_keys, allocator)) {
        pb_free(allocator, self, sizeof(pb_ratelimiter_fixed_window_state));
        return NULL;
    }

    self->window_start = (uint64_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint64_t));
    self->count = (uint32_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint32_t));
    if (self->window_start == NULL || self->count == NULL) {
        pb_free(allocator, self->window_start, (size_t)max_keys * sizeof(uint64_t));
        pb_free(allocator, self->count, (size_t)max_keys * sizeof(uint32_t));
        pb_map_destroy(&self->index, allocator);
        pb_free(allocator, self, sizeof(pb_ratelimiter_fixed_window_state));
        return NULL;
    }

    return (pb_ratelimiter *)self;
}

static void fixed_window_destroy(pb_ratelimiter *limiter)
{
    pb_ratelimiter_fixed_window_state *self = (pb_ratelimiter_fixed_window_state *)limiter;
    const pb_allocator *allocator;

    if (self == NULL) {
        return;
    }
    allocator = self->allocator;
    pb_free(allocator, self->window_start, (size_t)self->max_keys * sizeof(uint64_t));
    pb_free(allocator, self->count, (size_t)self->max_keys * sizeof(uint32_t));
    pb_map_destroy(&self->index, allocator);
    pb_free(allocator, self, sizeof(pb_ratelimiter_fixed_window_state));
}

static bool fixed_window_allow(pb_ratelimiter *limiter, uint64_t key, uint32_t cost,
                               uint64_t now_ms)
{
    pb_ratelimiter_fixed_window_state *self = (pb_ratelimiter_fixed_window_state *)limiter;
    uint64_t start;
    uint32_t slot;

    assert(self != NULL);

    if (!fixed_window_slot_for(self, key, &slot)) {
        return false;
    }

    start = fixed_window_window_of(self, now_ms);
    if (self->window_start[slot] != start) {
        /* A new window: the old count is gone, however recently it was earned.
         * This discontinuity is the whole trade-off. A slot claimed for a key
         * never seen starts at window 0 with a count of 0, so it needs no
         * special case — either the window differs and it resets, or `now` is
         * genuinely in window 0 and the count is already right. */
        self->window_start[slot] = start;
        self->count[slot] = 0u;
    }

    if ((uint64_t)self->count[slot] + (uint64_t)cost > (uint64_t)self->limit) {
        return false;
    }
    self->count[slot] += cost;
    return true;
}

static uint64_t fixed_window_retry_after(pb_ratelimiter *limiter, uint64_t key, uint64_t now_ms)
{
    pb_ratelimiter_fixed_window_state *self = (pb_ratelimiter_fixed_window_state *)limiter;
    uint64_t start;
    uint32_t slot;

    assert(self != NULL);

    if (!pb_map_get(&self->index, key, &slot)) {
        /* Untracked. With room in the table this key would be admitted right
         * now, so zero is the truth; with the table full it will never be
         * admitted, and zero would be a lie a caller acts on. */
        return self->used >= self->max_keys ? PB_RATELIMITER_RETRY_UNKNOWN : 0u;
    }

    start = fixed_window_window_of(self, now_ms);
    if (self->window_start[slot] != start) {
        return 0u;
    }
    if (self->count[slot] < self->limit) {
        return 0u;
    }
    return start + (uint64_t)self->window_ms - now_ms;
}

static size_t fixed_window_state_size(const pb_ratelimiter *limiter)
{
    const pb_ratelimiter_fixed_window_state *self =
        (const pb_ratelimiter_fixed_window_state *)limiter;
    assert(self != NULL);
    return (size_t)self->used;
}

static size_t fixed_window_memory_bytes(const pb_ratelimiter *limiter)
{
    const pb_ratelimiter_fixed_window_state *self =
        (const pb_ratelimiter_fixed_window_state *)limiter;
    assert(self != NULL);
    return sizeof(pb_ratelimiter_fixed_window_state) +
           (size_t)self->max_keys * (sizeof(uint64_t) + sizeof(uint32_t)) +
           pb_map_memory_bytes(&self->index);
}

const pb_ratelimiter_vtable pb_ratelimiter_fixed_window = { fixed_window_create,
                                                            fixed_window_allow,
                                                            fixed_window_retry_after,
                                                            fixed_window_state_size,
                                                            fixed_window_memory_bytes,
                                                            fixed_window_destroy };
