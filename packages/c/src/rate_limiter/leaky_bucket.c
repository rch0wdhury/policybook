/*
 * GENERATED COPY — do not edit. Edit policies/rate-limiter/leaky-bucket/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * LeakyBucket — a level that rises with each request and drains at a steady rate.
 *
 * Mirrors index.ts and policy.py, and mirrors token_bucket.c line for line
 * under the substitution `tokens = capacity - level`. Keeping the two in step
 * is deliberate: a vector in each policy pins the equivalence, so a change to
 * one that is not made to the other fails a test rather than drifting quietly.
 */

#include "policybook/rate_limiter/leaky_bucket.h"

#include <assert.h>
#include <stddef.h>

#include "policybook/allocator.h"
#include "policybook/ds/map.h"

typedef struct pb_ratelimiter_leaky_bucket_state {
    const pb_allocator *allocator;
    pb_map index;     /* key -> slot */
    uint32_t *level;  /* whole units currently in the bucket */
    uint32_t *credit; /* thousandths of a unit drained, always 0..999 */
    uint64_t *last;   /* when the slot was last brought up to date */
    uint32_t rate_per_sec;
    uint32_t capacity;
    uint32_t drain_ms; /* how long a full bucket takes to drain */
    uint32_t max_keys;
    uint32_t used;
} pb_ratelimiter_leaky_bucket_state;

/*
 * Let a bucket leak up to `now_ms`.
 *
 * Idle time is clamped to `drain_ms` before the multiply, so a key untouched
 * for a month cannot overflow the arithmetic. The result is identical either
 * way, because the bucket empties long before.
 */
static void drain(pb_ratelimiter_leaky_bucket_state *self, uint32_t slot, uint64_t now_ms)
{
    uint64_t elapsed;
    uint64_t credit;
    uint64_t drained;

    if (now_ms <= self->last[slot]) {
        return;
    }
    elapsed = now_ms - self->last[slot];
    if (elapsed > (uint64_t)self->drain_ms) {
        elapsed = (uint64_t)self->drain_ms;
    }

    credit = (uint64_t)self->credit[slot] + (uint64_t)self->rate_per_sec * elapsed;
    drained = credit / 1000u;
    credit %= 1000u;

    if (drained >= (uint64_t)self->level[slot]) {
        /* The bucket has run dry. Nothing further leaks, and the fraction that
         * would have leaked next is discarded. */
        self->level[slot] = 0u;
        credit = 0u;
    } else {
        self->level[slot] -= (uint32_t)drained;
    }

    self->credit[slot] = (uint32_t)credit;
    self->last[slot] = now_ms;
}

/*
 * Find a key's slot, claiming a free one if it has never been seen.
 *
 * Returns false when the table is full, which is the fail-closed case: a new
 * key is refused rather than being silently let through.
 */
static bool leaky_bucket_slot_for(pb_ratelimiter_leaky_bucket_state *self, uint64_t key, uint64_t now_ms,
                     uint32_t *slot)
{
    if (pb_map_get(&self->index, key, slot)) {
        drain(self, *slot, now_ms);
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
    /* A key never seen starts empty: it has been draining for all of history. */
    self->level[*slot] = 0u;
    self->credit[*slot] = 0u;
    self->last[*slot] = now_ms;
    return true;
}

static pb_ratelimiter *leaky_bucket_create(const void *params, const pb_allocator *allocator,
                                           pb_rng *rng)
{
    const pb_ratelimiter_leaky_bucket_params *config =
        (const pb_ratelimiter_leaky_bucket_params *)params;
    pb_ratelimiter_leaky_bucket_state *self;
    uint32_t rate_per_sec;
    uint32_t capacity;
    uint32_t max_keys;

    (void)rng; /* a leaky bucket makes no random choices */

    rate_per_sec = (config == NULL) ? 100u : config->rate_per_sec;
    capacity = (config == NULL) ? 1u : config->capacity;
    max_keys = (config == NULL) ? 1024u : config->max_keys;
    if (rate_per_sec == 0u || capacity == 0u || max_keys == 0u) {
        return NULL;
    }

    self = (pb_ratelimiter_leaky_bucket_state *)pb_alloc(
        allocator, sizeof(pb_ratelimiter_leaky_bucket_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->rate_per_sec = rate_per_sec;
    self->capacity = capacity;
    /* Ceiling division, in 64 bits so a large capacity cannot overflow. */
    self->drain_ms =
        (uint32_t)(((uint64_t)capacity * 1000u + (uint64_t)rate_per_sec - 1u) / rate_per_sec);
    self->max_keys = max_keys;
    self->used = 0u;
    self->level = NULL;
    self->credit = NULL;
    self->last = NULL;

    if (!pb_map_init(&self->index, max_keys, allocator)) {
        pb_free(allocator, self, sizeof(pb_ratelimiter_leaky_bucket_state));
        return NULL;
    }

    self->level = (uint32_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint32_t));
    self->credit = (uint32_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint32_t));
    self->last = (uint64_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint64_t));
    if (self->level == NULL || self->credit == NULL || self->last == NULL) {
        pb_free(allocator, self->level, (size_t)max_keys * sizeof(uint32_t));
        pb_free(allocator, self->credit, (size_t)max_keys * sizeof(uint32_t));
        pb_free(allocator, self->last, (size_t)max_keys * sizeof(uint64_t));
        pb_map_destroy(&self->index, allocator);
        pb_free(allocator, self, sizeof(pb_ratelimiter_leaky_bucket_state));
        return NULL;
    }

    return (pb_ratelimiter *)self;
}

static void leaky_bucket_destroy(pb_ratelimiter *limiter)
{
    pb_ratelimiter_leaky_bucket_state *self = (pb_ratelimiter_leaky_bucket_state *)limiter;
    const pb_allocator *allocator;

    if (self == NULL) {
        return;
    }
    allocator = self->allocator;
    pb_free(allocator, self->level, (size_t)self->max_keys * sizeof(uint32_t));
    pb_free(allocator, self->credit, (size_t)self->max_keys * sizeof(uint32_t));
    pb_free(allocator, self->last, (size_t)self->max_keys * sizeof(uint64_t));
    pb_map_destroy(&self->index, allocator);
    pb_free(allocator, self, sizeof(pb_ratelimiter_leaky_bucket_state));
}

static bool leaky_bucket_allow(pb_ratelimiter *limiter, uint64_t key, uint32_t cost,
                               uint64_t now_ms)
{
    pb_ratelimiter_leaky_bucket_state *self = (pb_ratelimiter_leaky_bucket_state *)limiter;
    uint32_t slot;

    assert(self != NULL);

    if (!leaky_bucket_slot_for(self, key, now_ms, &slot)) {
        return false;
    }
    if ((uint64_t)self->level[slot] + (uint64_t)cost > (uint64_t)self->capacity) {
        return false;
    }
    self->level[slot] += cost;
    return true;
}

static uint64_t leaky_bucket_retry_after(pb_ratelimiter *limiter, uint64_t key, uint64_t now_ms)
{
    pb_ratelimiter_leaky_bucket_state *self = (pb_ratelimiter_leaky_bucket_state *)limiter;
    uint32_t slot;
    uint32_t deficit;

    assert(self != NULL);

    if (!pb_map_get(&self->index, key, &slot)) {
        /* Untracked. With room in the table this key would be admitted right
         * now, so zero is the truth; with the table full it will never be
         * admitted, and zero would be a lie a caller acts on. */
        return self->used >= self->max_keys ? PB_RATELIMITER_RETRY_UNKNOWN : 0u;
    }

    drain(self, slot, now_ms);
    if (self->level[slot] < self->capacity) {
        return 0u;
    }

    deficit = 1000u - self->credit[slot];
    return ((uint64_t)deficit + (uint64_t)self->rate_per_sec - 1u) / (uint64_t)self->rate_per_sec;
}

static size_t leaky_bucket_state_size(const pb_ratelimiter *limiter)
{
    const pb_ratelimiter_leaky_bucket_state *self =
        (const pb_ratelimiter_leaky_bucket_state *)limiter;
    assert(self != NULL);
    return (size_t)self->used;
}

static size_t leaky_bucket_memory_bytes(const pb_ratelimiter *limiter)
{
    const pb_ratelimiter_leaky_bucket_state *self =
        (const pb_ratelimiter_leaky_bucket_state *)limiter;
    assert(self != NULL);
    return sizeof(pb_ratelimiter_leaky_bucket_state) +
           (size_t)self->max_keys * (2u * sizeof(uint32_t) + sizeof(uint64_t)) +
           pb_map_memory_bytes(&self->index);
}

const pb_ratelimiter_vtable pb_ratelimiter_leaky_bucket = { leaky_bucket_create,
                                                            leaky_bucket_allow,
                                                            leaky_bucket_retry_after,
                                                            leaky_bucket_state_size,
                                                            leaky_bucket_memory_bytes,
                                                            leaky_bucket_destroy };
