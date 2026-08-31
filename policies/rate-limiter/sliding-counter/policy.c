/*
 * SlidingCounter — two fixed windows, weighted by how far into the new one you are.
 *
 * Mirrors index.ts and policy.py. A map from key to slot, and three parallel
 * arrays: which window the slot is in, its count, and the previous window's.
 */

#include "policybook/rate_limiter/sliding_counter.h"

#include <assert.h>
#include <stddef.h>

#include "policybook/allocator.h"
#include "policybook/ds/map.h"

/* A slot that has never been rolled forward. No real window start can equal it,
 * because window starts are multiples of window_ms below any plausible clock. */
#define PB_SLIDING_COUNTER_UNSET UINT64_MAX

typedef struct pb_ratelimiter_sliding_counter_state {
    const pb_allocator *allocator;
    pb_map index; /* key -> slot */
    uint64_t *window_start;
    uint32_t *current;
    uint32_t *previous;
    uint32_t limit;
    uint32_t window_ms;
    uint32_t max_keys;
    uint32_t used;
} pb_ratelimiter_sliding_counter_state;

/* The start of the window containing `now`. Integer division, no floats. */
static uint64_t sliding_counter_window_of(const pb_ratelimiter_sliding_counter_state *self, uint64_t now)
{
    return now - (now % (uint64_t)self->window_ms);
}

/* Roll a slot's counters forward to `start`. */
static void sliding_counter_advance(pb_ratelimiter_sliding_counter_state *self, uint32_t slot, uint64_t start)
{
    uint64_t previous_start = self->window_start[slot];

    if (previous_start == start) {
        return;
    }

    if (previous_start != PB_SLIDING_COUNTER_UNSET &&
        start == previous_start + (uint64_t)self->window_ms) {
        /* The very next window: today's count becomes yesterday's. */
        self->previous[slot] = self->current[slot];
    } else {
        /* A gap of two windows or more — nothing from before is still in the
         * trailing window, so both counts go. */
        self->previous[slot] = 0u;
    }
    self->current[slot] = 0u;
    self->window_start[slot] = start;
}

/*
 * The weighted estimate of requests in the trailing window.
 *
 * `previous * (window_ms - elapsed) / window_ms + current`, with the remainder
 * discarded. The multiply is done in 64 bits so a large limit cannot overflow
 * before the division brings it back down.
 */
static uint64_t estimate(const pb_ratelimiter_sliding_counter_state *self, uint32_t slot,
                         uint64_t now_ms)
{
    uint64_t elapsed = now_ms - self->window_start[slot];
    uint64_t carried = ((uint64_t)self->previous[slot] * ((uint64_t)self->window_ms - elapsed)) /
                       (uint64_t)self->window_ms;
    return carried + (uint64_t)self->current[slot];
}

/*
 * Find a key's slot, claiming a free one if it has never been seen.
 *
 * Returns false when the table is full, which is the fail-closed case: a new
 * key is refused rather than being silently let through.
 */
static bool sliding_counter_slot_for(pb_ratelimiter_sliding_counter_state *self, uint64_t key, uint32_t *slot)
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
    self->window_start[*slot] = PB_SLIDING_COUNTER_UNSET;
    self->current[*slot] = 0u;
    self->previous[*slot] = 0u;
    return true;
}

static pb_ratelimiter *sliding_counter_create(const void *params, const pb_allocator *allocator,
                                              pb_rng *rng)
{
    const pb_ratelimiter_sliding_counter_params *config =
        (const pb_ratelimiter_sliding_counter_params *)params;
    pb_ratelimiter_sliding_counter_state *self;
    uint32_t limit;
    uint32_t window_ms;
    uint32_t max_keys;

    (void)rng; /* a sliding counter makes no random choices */

    limit = (config == NULL) ? 100u : config->limit;
    window_ms = (config == NULL) ? 1000u : config->window_ms;
    max_keys = (config == NULL) ? 1024u : config->max_keys;
    if (limit == 0u || window_ms == 0u || max_keys == 0u) {
        return NULL;
    }

    self = (pb_ratelimiter_sliding_counter_state *)pb_alloc(
        allocator, sizeof(pb_ratelimiter_sliding_counter_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->limit = limit;
    self->window_ms = window_ms;
    self->max_keys = max_keys;
    self->used = 0u;
    self->window_start = NULL;
    self->current = NULL;
    self->previous = NULL;

    if (!pb_map_init(&self->index, max_keys, allocator)) {
        pb_free(allocator, self, sizeof(pb_ratelimiter_sliding_counter_state));
        return NULL;
    }

    self->window_start = (uint64_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint64_t));
    self->current = (uint32_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint32_t));
    self->previous = (uint32_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint32_t));
    if (self->window_start == NULL || self->current == NULL || self->previous == NULL) {
        pb_free(allocator, self->window_start, (size_t)max_keys * sizeof(uint64_t));
        pb_free(allocator, self->current, (size_t)max_keys * sizeof(uint32_t));
        pb_free(allocator, self->previous, (size_t)max_keys * sizeof(uint32_t));
        pb_map_destroy(&self->index, allocator);
        pb_free(allocator, self, sizeof(pb_ratelimiter_sliding_counter_state));
        return NULL;
    }

    return (pb_ratelimiter *)self;
}

static void sliding_counter_destroy(pb_ratelimiter *limiter)
{
    pb_ratelimiter_sliding_counter_state *self = (pb_ratelimiter_sliding_counter_state *)limiter;
    const pb_allocator *allocator;

    if (self == NULL) {
        return;
    }
    allocator = self->allocator;
    pb_free(allocator, self->window_start, (size_t)self->max_keys * sizeof(uint64_t));
    pb_free(allocator, self->current, (size_t)self->max_keys * sizeof(uint32_t));
    pb_free(allocator, self->previous, (size_t)self->max_keys * sizeof(uint32_t));
    pb_map_destroy(&self->index, allocator);
    pb_free(allocator, self, sizeof(pb_ratelimiter_sliding_counter_state));
}

static bool sliding_counter_allow(pb_ratelimiter *limiter, uint64_t key, uint32_t cost,
                                  uint64_t now_ms)
{
    pb_ratelimiter_sliding_counter_state *self = (pb_ratelimiter_sliding_counter_state *)limiter;
    uint32_t slot;

    assert(self != NULL);

    if (!sliding_counter_slot_for(self, key, &slot)) {
        return false;
    }

    sliding_counter_advance(self, slot, sliding_counter_window_of(self, now_ms));
    if (estimate(self, slot, now_ms) + (uint64_t)cost > (uint64_t)self->limit) {
        return false;
    }
    self->current[slot] += cost;
    return true;
}

static uint64_t sliding_counter_retry_after(pb_ratelimiter *limiter, uint64_t key,
                                            uint64_t now_ms)
{
    pb_ratelimiter_sliding_counter_state *self = (pb_ratelimiter_sliding_counter_state *)limiter;
    uint64_t start;
    uint64_t elapsed;
    uint64_t target;
    uint32_t slot;
    uint32_t current;
    uint32_t previous;
    uint32_t need;
    uint32_t excess;

    assert(self != NULL);

    if (!pb_map_get(&self->index, key, &slot)) {
        /* Untracked. With room in the table this key would be admitted right
         * now, so zero is the truth; with the table full it will never be
         * admitted, and zero would be a lie a caller acts on. */
        return self->used >= self->max_keys ? PB_RATELIMITER_RETRY_UNKNOWN : 0u;
    }

    start = sliding_counter_window_of(self, now_ms);
    sliding_counter_advance(self, slot, start);
    if (estimate(self, slot, now_ms) < (uint64_t)self->limit) {
        return 0u;
    }

    current = self->current[slot];
    previous = self->previous[slot];

    if (current + 1u > self->limit) {
        /* The current window has reached the limit on its own, so the wait runs
         * past the edge — but not only to the edge. At the edge this count
         * becomes the previous count and, undecayed, still refuses. `allow`
         * never lets `current` exceed `limit`, so one further millisecond of
         * decay is always enough. */
        return start + (uint64_t)self->window_ms - now_ms + 1u;
    }
    if (previous == 0u) {
        return 0u;
    }

    /* Admission needs the carried part to fall to `limit - current - 1` or
     * below. It decays linearly, so the smallest qualifying elapsed follows
     * directly:
     *
     *   carried <= need  <=>  previous * (window_ms - elapsed) < (need + 1) * window_ms
     *                    <=>  elapsed > window_ms * (previous - need - 1) / previous
     *
     * An excess of zero means the carried count is exactly one too high, which
     * still needs a millisecond of decay — so only a smaller previous admits
     * immediately. */
    need = self->limit - current - 1u;
    if (previous < need + 1u) {
        return 0u;
    }
    excess = previous - need - 1u;

    target = ((uint64_t)self->window_ms * (uint64_t)excess) / (uint64_t)previous + 1u;
    elapsed = now_ms - start;
    return target > elapsed ? target - elapsed : 0u;
}

static size_t sliding_counter_state_size(const pb_ratelimiter *limiter)
{
    const pb_ratelimiter_sliding_counter_state *self =
        (const pb_ratelimiter_sliding_counter_state *)limiter;
    assert(self != NULL);
    return (size_t)self->used;
}

static size_t sliding_counter_memory_bytes(const pb_ratelimiter *limiter)
{
    const pb_ratelimiter_sliding_counter_state *self =
        (const pb_ratelimiter_sliding_counter_state *)limiter;
    assert(self != NULL);
    return sizeof(pb_ratelimiter_sliding_counter_state) +
           (size_t)self->max_keys * (sizeof(uint64_t) + 2u * sizeof(uint32_t)) +
           pb_map_memory_bytes(&self->index);
}

const pb_ratelimiter_vtable pb_ratelimiter_sliding_counter = { sliding_counter_create,
                                                               sliding_counter_allow,
                                                               sliding_counter_retry_after,
                                                               sliding_counter_state_size,
                                                               sliding_counter_memory_bytes,
                                                               sliding_counter_destroy };
