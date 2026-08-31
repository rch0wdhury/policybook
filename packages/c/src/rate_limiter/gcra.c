/*
 * GENERATED COPY — do not edit. Edit policies/rate-limiter/gcra/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * Gcra — the token bucket, kept as one number instead of three.
 *
 * Mirrors index.ts and policy.py. A map from key to slot and a single array of
 * theoretical arrival times — the whole state, and the reason to prefer this
 * over token_bucket.c when keys are many.
 *
 * The TAT is signed rather than unsigned. A key's schedule can legitimately sit
 * *behind* the clock (that is what an idle bucket looks like), and the
 * conformance test subtracts the tolerance from it, so intermediate values go
 * negative for any key seen early in a run. Unsigned arithmetic would wrap
 * those into enormous positives and refuse everything.
 */

#include "policybook/rate_limiter/gcra.h"

#include <assert.h>
#include <stddef.h>

#include "policybook/allocator.h"
#include "policybook/ds/map.h"

/* Scaled units in one permit. One millisecond is `rate_per_sec` of them. */
#define PB_GCRA_UNIT 1000

typedef struct pb_ratelimiter_gcra_state {
    const pb_allocator *allocator;
    pb_map index; /* key -> slot */
    int64_t *tat; /* theoretical arrival time, in scaled units */
    int64_t tolerance;
    uint32_t rate_per_sec;
    uint32_t burst;
    uint32_t max_keys;
    uint32_t used;
} pb_ratelimiter_gcra_state;

/*
 * Find a key's slot, claiming a free one if it has never been seen.
 *
 * A fresh slot is seeded with the current scaled time, which is what "fully
 * conforming" means: the schedule starts now, so the whole burst is available.
 *
 * Returns false when the table is full, which is the fail-closed case: a new
 * key is refused rather than being silently let through.
 */
static bool gcra_slot_for(pb_ratelimiter_gcra_state *self, uint64_t key, int64_t scaled,
                     uint32_t *slot)
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
    self->tat[*slot] = scaled;
    return true;
}

static pb_ratelimiter *gcra_create(const void *params, const pb_allocator *allocator,
                                   pb_rng *rng)
{
    const pb_ratelimiter_gcra_params *config = (const pb_ratelimiter_gcra_params *)params;
    pb_ratelimiter_gcra_state *self;
    uint32_t rate_per_sec;
    uint32_t burst;
    uint32_t max_keys;

    (void)rng; /* GCRA makes no random choices */

    rate_per_sec = (config == NULL) ? 100u : config->rate_per_sec;
    burst = (config == NULL) ? 100u : config->burst;
    max_keys = (config == NULL) ? 1024u : config->max_keys;
    if (rate_per_sec == 0u || burst == 0u || max_keys == 0u) {
        return NULL;
    }

    self = (pb_ratelimiter_gcra_state *)pb_alloc(allocator, sizeof(pb_ratelimiter_gcra_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->rate_per_sec = rate_per_sec;
    self->burst = burst;
    self->tolerance = (int64_t)(burst - 1u) * PB_GCRA_UNIT;
    self->max_keys = max_keys;
    self->used = 0u;
    self->tat = NULL;

    if (!pb_map_init(&self->index, max_keys, allocator)) {
        pb_free(allocator, self, sizeof(pb_ratelimiter_gcra_state));
        return NULL;
    }

    self->tat = (int64_t *)pb_alloc(allocator, (size_t)max_keys * sizeof(int64_t));
    if (self->tat == NULL) {
        pb_map_destroy(&self->index, allocator);
        pb_free(allocator, self, sizeof(pb_ratelimiter_gcra_state));
        return NULL;
    }

    return (pb_ratelimiter *)self;
}

static void gcra_destroy(pb_ratelimiter *limiter)
{
    pb_ratelimiter_gcra_state *self = (pb_ratelimiter_gcra_state *)limiter;
    const pb_allocator *allocator;

    if (self == NULL) {
        return;
    }
    allocator = self->allocator;
    pb_free(allocator, self->tat, (size_t)self->max_keys * sizeof(int64_t));
    pb_map_destroy(&self->index, allocator);
    pb_free(allocator, self, sizeof(pb_ratelimiter_gcra_state));
}

static bool gcra_allow(pb_ratelimiter *limiter, uint64_t key, uint32_t cost, uint64_t now_ms)
{
    pb_ratelimiter_gcra_state *self = (pb_ratelimiter_gcra_state *)limiter;
    int64_t scaled;
    int64_t tat;
    uint32_t slot;

    assert(self != NULL);

    /* A cost above the burst can never be met. The conformance test below does
     * not cap on its own — an idle key's TAT sits arbitrarily far in the past —
     * so the ceiling has to be stated. */
    if (cost > self->burst) {
        return false;
    }

    scaled = (int64_t)now_ms * (int64_t)self->rate_per_sec;
    if (!gcra_slot_for(self, key, scaled, &slot)) {
        return false;
    }
    tat = self->tat[slot];

    /* Signed before the subtraction: at cost 0 the reference computes
     * (cost - 1) * UNIT = -UNIT, where `cost - 1u` would wrap to 2^32 - 1. */
    if (scaled < tat - self->tolerance + ((int64_t)cost - 1) * PB_GCRA_UNIT) {
        return false;
    }

    /* `max` is what stops an idle key banking unbounded credit: the schedule
     * restarts from now rather than from a TAT left far in the past. */
    self->tat[slot] = (scaled > tat ? scaled : tat) + (int64_t)cost * PB_GCRA_UNIT;
    return true;
}

static uint64_t gcra_retry_after(pb_ratelimiter *limiter, uint64_t key, uint64_t now_ms)
{
    pb_ratelimiter_gcra_state *self = (pb_ratelimiter_gcra_state *)limiter;
    int64_t scaled;
    int64_t target;
    int64_t at;
    uint32_t slot;

    assert(self != NULL);

    if (!pb_map_get(&self->index, key, &slot)) {
        /* Untracked. With room in the table this key would be admitted right
         * now, so zero is the truth; with the table full it will never be
         * admitted, and zero would be a lie a caller acts on. */
        return self->used >= self->max_keys ? PB_RATELIMITER_RETRY_UNKNOWN : 0u;
    }

    scaled = (int64_t)now_ms * (int64_t)self->rate_per_sec;
    target = self->tat[slot] - self->tolerance;
    if (scaled >= target) {
        return 0u;
    }

    /* The first whole millisecond at or past the target. `target` is positive
     * here, since it exceeds a non-negative `scaled`. */
    at = (target + (int64_t)self->rate_per_sec - 1) / (int64_t)self->rate_per_sec;
    return (uint64_t)(at - (int64_t)now_ms);
}

static size_t gcra_state_size(const pb_ratelimiter *limiter)
{
    const pb_ratelimiter_gcra_state *self = (const pb_ratelimiter_gcra_state *)limiter;
    assert(self != NULL);
    return (size_t)self->used;
}

static size_t gcra_memory_bytes(const pb_ratelimiter *limiter)
{
    const pb_ratelimiter_gcra_state *self = (const pb_ratelimiter_gcra_state *)limiter;
    assert(self != NULL);
    return sizeof(pb_ratelimiter_gcra_state) + (size_t)self->max_keys * sizeof(int64_t) +
           pb_map_memory_bytes(&self->index);
}

const pb_ratelimiter_vtable pb_ratelimiter_gcra = { gcra_create,     gcra_allow,
                                                    gcra_retry_after, gcra_state_size,
                                                    gcra_memory_bytes, gcra_destroy };
