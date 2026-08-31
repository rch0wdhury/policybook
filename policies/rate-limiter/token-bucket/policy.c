/*
 * TokenBucket — spend from a balance that refills at a steady rate.
 *
 * Mirrors index.ts and policy.py. A map from key to slot, and three parallel
 * arrays: the whole tokens, the thousandths carried between refills, and when
 * the slot was last touched.
 */

#include "policybook/rate_limiter/token_bucket.h"

#include <assert.h>
#include <stddef.h>

#include "policybook/allocator.h"
#include "policybook/ds/map.h"

typedef struct pb_ratelimiter_token_bucket_state {
    const pb_allocator *allocator;
    pb_map index;     /* key -> slot */
    uint32_t *tokens; /* whole tokens available */
    uint32_t *credit; /* thousandths of a token, always 0..999 */
    uint64_t *last;   /* when the slot was last brought up to date */
    uint32_t rate_per_sec;
    uint32_t burst;
    uint32_t fill_ms; /* how long an empty bucket takes to fill */
    uint32_t max_keys;
    uint32_t used;
} pb_ratelimiter_token_bucket_state;

/*
 * Bring a bucket up to date at `now_ms`.
 *
 * Idle time is clamped to `fill_ms` before the multiply. Without it a key
 * untouched for a month would compute `rate_per_sec * elapsed` far outside
 * 32 bits; the result is identical either way, because the bucket saturates
 * long before.
 */
static void token_bucket_refill(pb_ratelimiter_token_bucket_state *self, uint32_t slot, uint64_t now_ms)
{
    uint64_t elapsed;
    uint64_t credit;
    uint64_t tokens;

    if (now_ms <= self->last[slot]) {
        return;
    }
    elapsed = now_ms - self->last[slot];
    if (elapsed > (uint64_t)self->fill_ms) {
        elapsed = (uint64_t)self->fill_ms;
    }

    credit = (uint64_t)self->credit[slot] + (uint64_t)self->rate_per_sec * elapsed;
    tokens = (uint64_t)self->tokens[slot] + credit / 1000u;
    credit %= 1000u;

    if (tokens >= (uint64_t)self->burst) {
        /* The bucket overflows: tokens above `burst` are lost, and so is the
         * fraction that would have become the next one. */
        tokens = (uint64_t)self->burst;
        credit = 0u;
    }

    self->tokens[slot] = (uint32_t)tokens;
    self->credit[slot] = (uint32_t)credit;
    self->last[slot] = now_ms;
}

/*
 * Find a key's slot, claiming a free one if it has never been seen.
 *
 * Returns false when the table is full, which is the fail-closed case: a new
 * key is refused rather than being silently let through.
 */
static bool token_bucket_slot_for(pb_ratelimiter_token_bucket_state *self, uint64_t key, uint64_t now_ms,
                     uint32_t *slot)
{
    if (pb_map_get(&self->index, key, slot)) {
        token_bucket_refill(self, *slot, now_ms);
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
    /* A key never seen starts full: it has been idle for all of history. */
    self->tokens[*slot] = self->burst;
    self->credit[*slot] = 0u;
    self->last[*slot] = now_ms;
    return true;
}

static pb_ratelimiter *token_bucket_create(const void *params, const pb_allocator *allocator,
                                           pb_rng *rng)
{
    const pb_ratelimiter_token_bucket_params *config =
        (const pb_ratelimiter_token_bucket_params *)params;
    pb_ratelimiter_token_bucket_state *self;
    uint32_t rate_per_sec;
    uint32_t burst;
    uint32_t max_keys;

    (void)rng; /* a token bucket makes no random choices */

    rate_per_sec = (config == NULL) ? 100u : config->rate_per_sec;
    burst = (config == NULL) ? 100u : config->burst;
    max_keys = (config == NULL) ? 1024u : config->max_keys;
    if (rate_per_sec == 0u || burst == 0u || max_keys == 0u) {
        return NULL;
    }

    self = (pb_ratelimiter_token_bucket_state *)pb_alloc(
        allocator, sizeof(pb_ratelimiter_token_bucket_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->rate_per_sec = rate_per_sec;
    self->burst = burst;
    /* Ceiling division, in 64 bits so a large burst cannot overflow. */
    self->fill_ms =
        (uint32_t)(((uint64_t)burst * 1000u + (uint64_t)rate_per_sec - 1u) / rate_per_sec);
    self->max_keys = max_keys;
    self->used = 0u;
    self->tokens = NULL;
    self->credit = NULL;
    self->last = NULL;

    if (!pb_map_init(&self->index, max_keys, allocator)) {
        pb_free(allocator, self, sizeof(pb_ratelimiter_token_bucket_state));
        return NULL;
    }

    self->tokens = (uint32_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint32_t));
    self->credit = (uint32_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint32_t));
    self->last = (uint64_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(uint64_t));
    if (self->tokens == NULL || self->credit == NULL || self->last == NULL) {
        pb_free(allocator, self->tokens, (size_t)max_keys * sizeof(uint32_t));
        pb_free(allocator, self->credit, (size_t)max_keys * sizeof(uint32_t));
        pb_free(allocator, self->last, (size_t)max_keys * sizeof(uint64_t));
        pb_map_destroy(&self->index, allocator);
        pb_free(allocator, self, sizeof(pb_ratelimiter_token_bucket_state));
        return NULL;
    }

    return (pb_ratelimiter *)self;
}

static void token_bucket_destroy(pb_ratelimiter *limiter)
{
    pb_ratelimiter_token_bucket_state *self = (pb_ratelimiter_token_bucket_state *)limiter;
    const pb_allocator *allocator;

    if (self == NULL) {
        return;
    }
    allocator = self->allocator;
    pb_free(allocator, self->tokens, (size_t)self->max_keys * sizeof(uint32_t));
    pb_free(allocator, self->credit, (size_t)self->max_keys * sizeof(uint32_t));
    pb_free(allocator, self->last, (size_t)self->max_keys * sizeof(uint64_t));
    pb_map_destroy(&self->index, allocator);
    pb_free(allocator, self, sizeof(pb_ratelimiter_token_bucket_state));
}

static bool token_bucket_allow(pb_ratelimiter *limiter, uint64_t key, uint32_t cost,
                               uint64_t now_ms)
{
    pb_ratelimiter_token_bucket_state *self = (pb_ratelimiter_token_bucket_state *)limiter;
    uint32_t slot;

    assert(self != NULL);

    if (!token_bucket_slot_for(self, key, now_ms, &slot)) {
        return false;
    }
    if (self->tokens[slot] < cost) {
        return false;
    }
    self->tokens[slot] -= cost;
    return true;
}

static uint64_t token_bucket_retry_after(pb_ratelimiter *limiter, uint64_t key, uint64_t now_ms)
{
    pb_ratelimiter_token_bucket_state *self = (pb_ratelimiter_token_bucket_state *)limiter;
    uint32_t slot;
    uint32_t deficit;

    assert(self != NULL);

    if (!pb_map_get(&self->index, key, &slot)) {
        /* Untracked. With room in the table this key would be admitted right
         * now, so zero is the truth; with the table full it will never be
         * admitted, and zero would be a lie a caller acts on. */
        return self->used >= self->max_keys ? PB_RATELIMITER_RETRY_UNKNOWN : 0u;
    }

    token_bucket_refill(self, slot, now_ms);
    if (self->tokens[slot] >= 1u) {
        return 0u;
    }

    /* Ceiling division: the token arrives at the first whole millisecond where
     * the credit reaches 1,000. */
    deficit = 1000u - self->credit[slot];
    return ((uint64_t)deficit + (uint64_t)self->rate_per_sec - 1u) / (uint64_t)self->rate_per_sec;
}

static size_t token_bucket_state_size(const pb_ratelimiter *limiter)
{
    const pb_ratelimiter_token_bucket_state *self =
        (const pb_ratelimiter_token_bucket_state *)limiter;
    assert(self != NULL);
    return (size_t)self->used;
}

static size_t token_bucket_memory_bytes(const pb_ratelimiter *limiter)
{
    const pb_ratelimiter_token_bucket_state *self =
        (const pb_ratelimiter_token_bucket_state *)limiter;
    assert(self != NULL);
    return sizeof(pb_ratelimiter_token_bucket_state) +
           (size_t)self->max_keys * (2u * sizeof(uint32_t) + sizeof(uint64_t)) +
           pb_map_memory_bytes(&self->index);
}

const pb_ratelimiter_vtable pb_ratelimiter_token_bucket = { token_bucket_create,
                                                            token_bucket_allow,
                                                            token_bucket_retry_after,
                                                            token_bucket_state_size,
                                                            token_bucket_memory_bytes,
                                                            token_bucket_destroy };
