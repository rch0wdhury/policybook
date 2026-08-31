/*
 * GENERATED COPY — do not edit. Edit policies/rate-limiter/sliding-log/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * SlidingLog — remember every request time, and count the recent ones.
 *
 * Mirrors index.ts and policy.py. A map from key to slot, and one flat block of
 * `max_keys * limit` timestamps carved into a ring per slot — one allocation
 * rather than one per key, so a key's first request costs nothing extra.
 */

#include "policybook/rate_limiter/sliding_log.h"

#include <assert.h>
#include <stddef.h>

#include "policybook/allocator.h"
#include "policybook/ds/map.h"

typedef struct pb_ratelimiter_sliding_log_state {
    const pb_allocator *allocator;
    pb_map index;    /* key -> slot */
    uint64_t *times; /* max_keys rings of `limit` timestamps, laid end to end */
    uint32_t *head;  /* oldest live entry in each ring */
    uint32_t *count; /* live entries in each ring */
    uint32_t limit;
    uint32_t window_ms;
    uint32_t max_keys;
    uint32_t used;
} pb_ratelimiter_sliding_log_state;

/* The base of a slot's ring inside the flat block. */
static uint64_t *ring_of(pb_ratelimiter_sliding_log_state *self, uint32_t slot)
{
    return self->times + (size_t)slot * (size_t)self->limit;
}

/*
 * Drop every timestamp that has left the window.
 *
 * The window is (now - window_ms, now]: an entry exactly window_ms old has
 * left it. That is what makes "at most `limit` in any window_ms interval" true
 * as stated rather than off by one at the edge.
 *
 * O(1) amortised: each timestamp is written once and dropped once, so this loop
 * does constant work per admitted request however bursty the arrivals are.
 */
static void expire(pb_ratelimiter_sliding_log_state *self, uint32_t slot, uint64_t now_ms)
{
    const uint64_t *ring = ring_of(self, slot);

    /* Unsigned arithmetic: before window_ms has elapsed nothing can have aged
     * out, and computing `now - window_ms` would wrap. */
    if (now_ms < (uint64_t)self->window_ms) {
        return;
    }

    while (self->count[slot] > 0u && ring[self->head[slot]] <= now_ms - (uint64_t)self->window_ms) {
        self->head[slot] = (self->head[slot] + 1u) % self->limit;
        self->count[slot] -= 1u;
    }
}

/*
 * Find a key's slot, claiming a free one if it has never been seen.
 *
 * Returns false when the table is full, which is the fail-closed case: a new
 * key is refused rather than being silently let through.
 */
static bool sliding_log_slot_for(pb_ratelimiter_sliding_log_state *self, uint64_t key, uint32_t *slot)
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
    self->head[*slot] = 0u;
    self->count[*slot] = 0u;
    return true;
}

static pb_ratelimiter *sliding_log_create(const void *params, const pb_allocator *allocator,
                                          pb_rng *rng)
{
    const pb_ratelimiter_sliding_log_params *config =
        (const pb_ratelimiter_sliding_log_params *)params;
    pb_ratelimiter_sliding_log_state *self;
    uint32_t limit;
    uint32_t window_ms;
    uint32_t max_keys;
    size_t entries;

    (void)rng; /* a sliding log makes no random choices */

    limit = (config == NULL) ? 100u : config->limit;
    window_ms = (config == NULL) ? 1000u : config->window_ms;
    max_keys = (config == NULL) ? 1024u : config->max_keys;
    if (limit == 0u || window_ms == 0u || max_keys == 0u) {
        return NULL;
    }

    self = (pb_ratelimiter_sliding_log_state *)pb_alloc(
        allocator, sizeof(pb_ratelimiter_sliding_log_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->limit = limit;
    self->window_ms = window_ms;
    self->max_keys = max_keys;
    self->used = 0u;
    self->times = NULL;
    self->head = NULL;
    self->count = NULL;

    if (!pb_map_init(&self->index, max_keys, allocator)) {
        pb_free(allocator, self, sizeof(pb_ratelimiter_sliding_log_state));
        return NULL;
    }

    entries = (size_t)max_keys * (size_t)limit;
    self->times = (uint64_t *)pb_alloc(allocator, entries * sizeof(uint64_t));
    self->head = (uint32_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint32_t));
    self->count = (uint32_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint32_t));
    if (self->times == NULL || self->head == NULL || self->count == NULL) {
        pb_free(allocator, self->times, entries * sizeof(uint64_t));
        pb_free(allocator, self->head, (size_t)max_keys * sizeof(uint32_t));
        pb_free(allocator, self->count, (size_t)max_keys * sizeof(uint32_t));
        pb_map_destroy(&self->index, allocator);
        pb_free(allocator, self, sizeof(pb_ratelimiter_sliding_log_state));
        return NULL;
    }

    return (pb_ratelimiter *)self;
}

static void sliding_log_destroy(pb_ratelimiter *limiter)
{
    pb_ratelimiter_sliding_log_state *self = (pb_ratelimiter_sliding_log_state *)limiter;
    const pb_allocator *allocator;

    if (self == NULL) {
        return;
    }
    allocator = self->allocator;
    pb_free(allocator, self->times,
            (size_t)self->max_keys * (size_t)self->limit * sizeof(uint64_t));
    pb_free(allocator, self->head, (size_t)self->max_keys * sizeof(uint32_t));
    pb_free(allocator, self->count, (size_t)self->max_keys * sizeof(uint32_t));
    pb_map_destroy(&self->index, allocator);
    pb_free(allocator, self, sizeof(pb_ratelimiter_sliding_log_state));
}

static bool sliding_log_allow(pb_ratelimiter *limiter, uint64_t key, uint32_t cost,
                              uint64_t now_ms)
{
    pb_ratelimiter_sliding_log_state *self = (pb_ratelimiter_sliding_log_state *)limiter;
    uint64_t *ring;
    uint32_t slot;
    uint32_t unit;

    assert(self != NULL);

    if (!sliding_log_slot_for(self, key, &slot)) {
        return false;
    }

    expire(self, slot, now_ms);
    if ((uint64_t)self->count[slot] + (uint64_t)cost > (uint64_t)self->limit) {
        return false;
    }

    /* A request costing n occupies n slots, all stamped with the same instant,
     * so they age out together. */
    ring = ring_of(self, slot);
    for (unit = 0u; unit < cost; ++unit) {
        ring[(self->head[slot] + self->count[slot]) % self->limit] = now_ms;
        self->count[slot] += 1u;
    }
    return true;
}

static uint64_t sliding_log_retry_after(pb_ratelimiter *limiter, uint64_t key, uint64_t now_ms)
{
    pb_ratelimiter_sliding_log_state *self = (pb_ratelimiter_sliding_log_state *)limiter;
    const uint64_t *ring;
    uint32_t slot;

    assert(self != NULL);

    if (!pb_map_get(&self->index, key, &slot)) {
        /* Untracked. With room in the table this key would be admitted right
         * now, so zero is the truth; with the table full it will never be
         * admitted, and zero would be a lie a caller acts on. */
        return self->used >= self->max_keys ? PB_RATELIMITER_RETRY_UNKNOWN : 0u;
    }

    expire(self, slot, now_ms);
    if (self->count[slot] < self->limit) {
        return 0u;
    }

    /* The oldest entry leaves the window at `time + window_ms`, because the
     * window excludes its far end — so that instant is when room appears, not
     * the millisecond after it. */
    ring = ring_of(self, slot);
    return ring[self->head[slot]] + (uint64_t)self->window_ms - now_ms;
}

static size_t sliding_log_state_size(const pb_ratelimiter *limiter)
{
    const pb_ratelimiter_sliding_log_state *self =
        (const pb_ratelimiter_sliding_log_state *)limiter;
    assert(self != NULL);
    return (size_t)self->used;
}

static size_t sliding_log_memory_bytes(const pb_ratelimiter *limiter)
{
    const pb_ratelimiter_sliding_log_state *self =
        (const pb_ratelimiter_sliding_log_state *)limiter;
    assert(self != NULL);
    return sizeof(pb_ratelimiter_sliding_log_state) +
           (size_t)self->max_keys * (size_t)self->limit * sizeof(uint64_t) +
           (size_t)self->max_keys * 2u * sizeof(uint32_t) +
           pb_map_memory_bytes(&self->index);
}

const pb_ratelimiter_vtable pb_ratelimiter_sliding_log = { sliding_log_create,
                                                           sliding_log_allow,
                                                           sliding_log_retry_after,
                                                           sliding_log_state_size,
                                                           sliding_log_memory_bytes,
                                                           sliding_log_destroy };
