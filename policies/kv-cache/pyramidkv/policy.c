/*
 * PyramidKV — spend more cache on early layers than late ones.
 *
 * Mirrors index.ts and policy.py. The selection machinery is snapkv's; what is
 * new is `pb_kvcache_pyramid_budget` and the eviction target, which is the
 * tighter of the caller's budget and this layer's share.
 */

#include "policybook/kv_cache/pyramidkv.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "policybook/allocator.h"

typedef struct pb_kvcache_pyramidkv_state {
    const pb_allocator *allocator;
    uint32_t effective; /* this layer's share, after redistribution */
    uint32_t recent_window;
    uint32_t obs_window;
    uint32_t pool_radius;
    uint32_t capacity;
    uint32_t size;
    uint32_t slot;
    uint32_t *positions;
    float *history; /* capacity * obs_window, position-major */
    double *sums;
    double *pooled;
    uint8_t *doomed;
} pb_kvcache_pyramidkv_state;

uint32_t pb_kvcache_pyramid_budget(uint32_t budget, uint32_t layer, uint32_t num_layers,
                                   uint32_t pyramid_ratio)
{
    uint64_t span;
    uint64_t numerator;
    uint64_t denominator;

    /* One layer has nothing to redistribute, and the denominator would be
     * zero. */
    if (num_layers <= 1u) {
        return budget;
    }

    /* uint64 throughout: the numerator reaches 2*budget*ratio*num_layers, which
     * overflows 32 bits for a large model at a large budget. */
    span = (uint64_t)num_layers - 1u;
    numerator = 2ull * (uint64_t)budget *
                ((uint64_t)pyramid_ratio * span - (uint64_t)layer * ((uint64_t)pyramid_ratio - 1u));
    denominator = ((uint64_t)pyramid_ratio + 1u) * span;
    return (uint32_t)(numerator / denominator);
}

static void pyramidkv_release(pb_kvcache_pyramidkv_state *self)
{
    size_t slots;

    if (self == NULL) {
        return;
    }
    slots = (size_t)self->capacity;
    pb_free(self->allocator, self->positions, slots * sizeof(uint32_t));
    pb_free(self->allocator, self->history,
            slots * (size_t)self->obs_window * sizeof(float));
    pb_free(self->allocator, self->sums, slots * sizeof(double));
    pb_free(self->allocator, self->pooled, slots * sizeof(double));
    pb_free(self->allocator, self->doomed, slots * sizeof(uint8_t));
    pb_free(self->allocator, self, sizeof(pb_kvcache_pyramidkv_state));
}

static pb_kvcache *pyramidkv_create(const void *params, const pb_allocator *allocator,
                                    pb_rng *rng)
{
    const pb_kvcache_pyramidkv_params *config = (const pb_kvcache_pyramidkv_params *)params;
    pb_kvcache_pyramidkv_state *self;
    uint32_t budget;
    uint32_t layer;
    uint32_t num_layers;
    uint32_t pyramid_ratio;
    uint32_t recent_window;
    uint32_t obs_window;
    uint32_t pool_kernel;
    uint32_t allocated;
    uint32_t effective;
    size_t slots;

    (void)rng; /* entirely deterministic */

    budget = (config == NULL) ? 512u : config->budget;
    layer = (config == NULL) ? 0u : config->layer;
    num_layers = (config == NULL) ? 1u : config->num_layers;
    pyramid_ratio = (config == NULL) ? 4u : config->pyramid_ratio;
    recent_window = (config == NULL) ? 32u : config->recent_window;
    obs_window = (config == NULL) ? 16u : config->obs_window;
    pool_kernel = (config == NULL) ? 7u : config->pool_kernel;

    /* The slot arrays hold at least budget + 1 entries; UINT32_MAX would wrap
     * that. */
    if (budget == 0u || budget == UINT32_MAX || num_layers == 0u || obs_window == 0u) {
        return NULL;
    }
    if (layer >= num_layers) {
        return NULL;
    }
    /* A ratio below one would invert the pyramid, which is a different policy. */
    if (pyramid_ratio == 0u) {
        return NULL;
    }
    if (recent_window >= budget) {
        return NULL;
    }
    if (pool_kernel == 0u || (pool_kernel % 2u) == 0u) {
        return NULL;
    }

    /* A deep layer's share can fall below the recent window, at which point the
     * cache would be smaller than its own protected region — a state the
     * selection rule cannot express. It keeps the window plus one instead. */
    allocated = pb_kvcache_pyramid_budget(budget, layer, num_layers, pyramid_ratio);
    effective = (allocated < recent_window + 1u) ? recent_window + 1u : allocated;

    self = (pb_kvcache_pyramidkv_state *)pb_alloc(allocator,
                                                  sizeof(pb_kvcache_pyramidkv_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->effective = effective;
    self->recent_window = recent_window;
    self->obs_window = obs_window;
    self->pool_radius = (pool_kernel - 1u) / 2u;
    /* Room for whichever cap is larger: a shallow layer's share exceeds the
     * average, and a caller may still drive this at the average budget. */
    self->capacity = ((effective > budget) ? effective : budget) + 1u;
    self->size = 0u;
    self->slot = 0u;
    slots = (size_t)self->capacity;

    self->positions = (uint32_t *)pb_alloc(allocator, slots * sizeof(uint32_t));
    self->history =
        (float *)pb_alloc(allocator, slots * (size_t)obs_window * sizeof(float));
    self->sums = (double *)pb_alloc(allocator, slots * sizeof(double));
    self->pooled = (double *)pb_alloc(allocator, slots * sizeof(double));
    self->doomed = (uint8_t *)pb_alloc(allocator, slots * sizeof(uint8_t));
    if (self->positions == NULL || self->history == NULL || self->sums == NULL ||
        self->pooled == NULL || self->doomed == NULL) {
        pyramidkv_release(self);
        return NULL;
    }

    memset(self->history, 0, (size_t)obs_window * sizeof(float));
    self->positions[0] = 0u;
    self->doomed[0] = 0u;
    self->size = 1u;
    return (pb_kvcache *)self;
}

static void pyramidkv_destroy(pb_kvcache *policy)
{
    pyramidkv_release((pb_kvcache_pyramidkv_state *)policy);
}

static void pyramidkv_on_decode_step(pb_kvcache *policy, uint32_t pos, const float *attn,
                                     size_t attn_len)
{
    pb_kvcache_pyramidkv_state *self = (pb_kvcache_pyramidkv_state *)policy;
    size_t shared;
    size_t i;

    assert(self != NULL);

    /* A null vector is inert, ring included: the window spans the last
     * obs_window observed steps, not the last obs_window calls. */
    if (attn != NULL) {
        shared = (attn_len < (size_t)self->size) ? attn_len : (size_t)self->size;
        for (i = 0; i < shared; ++i) {
            self->history[i * (size_t)self->obs_window + (size_t)self->slot] = attn[i];
        }

        self->slot += 1u;
        if (self->slot == self->obs_window) {
            self->slot = 0u;
        }
    }

    assert(self->size < self->capacity);
    if (self->size >= self->capacity) {
        return;
    }

    memset(&self->history[(size_t)self->size * (size_t)self->obs_window], 0,
           (size_t)self->obs_window * sizeof(float));
    self->positions[self->size] = pos;
    self->doomed[self->size] = 0u;
    self->size += 1u;
}

static size_t pyramidkv_evict(pb_kvcache *policy, uint32_t budget, uint32_t *victims,
                              size_t capacity)
{
    pb_kvcache_pyramidkv_state *self = (pb_kvcache_pyramidkv_state *)policy;
    uint32_t target;
    uint32_t protected_count;
    uint32_t evictable_end;
    uint32_t needed;
    uint32_t taken;
    uint32_t i;
    uint32_t read;
    uint32_t write = 0u;
    size_t written = 0;

    assert(self != NULL);
    assert(victims != NULL);

    /* The tighter of the caller's budget and this layer's share, so a deep
     * layer holds less than it was offered while never exceeding the ask. */
    target = (budget < self->effective) ? budget : self->effective;
    if (self->size <= target) {
        return 0;
    }
    needed = self->size - target;

    protected_count = (self->recent_window < self->size) ? self->recent_window : self->size;
    evictable_end = self->size - protected_count;
    if (needed > evictable_end) {
        needed = evictable_end;
    }

    if ((size_t)needed > capacity) {
        return 0;
    }

    for (i = 0u; i < self->size; ++i) {
        const float *record = &self->history[(size_t)i * (size_t)self->obs_window];
        double total = 0.0;
        uint32_t s;
        for (s = 0u; s < self->obs_window; ++s) {
            total += (double)record[s];
        }
        self->sums[i] = total;
    }

    for (i = 0u; i < self->size; ++i) {
        uint32_t lo = (i < self->pool_radius) ? 0u : i - self->pool_radius;
        uint32_t hi = i + self->pool_radius;
        double best;
        uint32_t j;
        if (hi >= self->size) {
            hi = self->size - 1u;
        }
        best = self->sums[lo];
        for (j = lo + 1u; j <= hi; ++j) {
            if (self->sums[j] > best) {
                best = self->sums[j];
            }
        }
        self->pooled[i] = best;
    }

    for (taken = 0u; taken < needed; ++taken) {
        uint32_t best = evictable_end;
        for (i = 0u; i < evictable_end; ++i) {
            if (self->doomed[i] != 0u) {
                continue;
            }
            if (best == evictable_end || self->pooled[i] < self->pooled[best]) {
                best = i;
            }
        }
        if (best == evictable_end) {
            break;
        }
        self->doomed[best] = 1u;
    }

    for (read = 0u; read < self->size; ++read) {
        if (self->doomed[read] != 0u) {
            self->doomed[read] = 0u;
            victims[written] = self->positions[read];
            written += 1;
            continue;
        }
        if (write != read) {
            self->positions[write] = self->positions[read];
            memmove(&self->history[(size_t)write * (size_t)self->obs_window],
                    &self->history[(size_t)read * (size_t)self->obs_window],
                    (size_t)self->obs_window * sizeof(float));
        }
        write += 1u;
    }
    self->size = write;
    return written;
}

static size_t pyramidkv_memory_bytes(const pb_kvcache *policy)
{
    const pb_kvcache_pyramidkv_state *self = (const pb_kvcache_pyramidkv_state *)policy;
    size_t slots;

    if (self == NULL) {
        return sizeof(pb_kvcache_pyramidkv_state);
    }
    slots = (size_t)self->capacity;
    return sizeof(pb_kvcache_pyramidkv_state) + slots * sizeof(uint32_t) +
           slots * (size_t)self->obs_window * sizeof(float) + slots * sizeof(double) +
           slots * sizeof(double) + slots * sizeof(uint8_t);
}

const pb_kvcache_vtable pb_kvcache_pyramidkv = { pyramidkv_create, pyramidkv_on_decode_step,
                                                 pyramidkv_evict, pyramidkv_memory_bytes,
                                                 pyramidkv_destroy };
