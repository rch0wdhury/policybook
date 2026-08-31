/*
 * GENERATED COPY — do not edit. Edit policies/kv-cache/snapkv/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * SnapKV — score on the last few steps, then max-pool across neighbours.
 *
 * Mirrors index.ts and policy.py. The history is position-major
 * (history[i * obs_window + slot]) so compaction moves each surviving
 * position's whole record in one contiguous memmove.
 */

#include "policybook/kv_cache/snapkv.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "policybook/allocator.h"

typedef struct pb_kvcache_snapkv_state {
    const pb_allocator *allocator;
    uint32_t recent_window;
    uint32_t obs_window;
    uint32_t pool_radius; /* (pool_kernel - 1) / 2 */
    uint32_t capacity;    /* budget + 1 */
    uint32_t size;
    uint32_t slot; /* ring slot the next step writes to */
    uint32_t *positions;
    float *history; /* capacity * obs_window, position-major */
    double *sums;   /* eviction scratch */
    double *pooled; /* eviction scratch */
    uint8_t *doomed;
} pb_kvcache_snapkv_state;

static void snapkv_release(pb_kvcache_snapkv_state *self)
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
    pb_free(self->allocator, self, sizeof(pb_kvcache_snapkv_state));
}

static pb_kvcache *snapkv_create(const void *params, const pb_allocator *allocator, pb_rng *rng)
{
    const pb_kvcache_snapkv_params *config = (const pb_kvcache_snapkv_params *)params;
    pb_kvcache_snapkv_state *self;
    uint32_t budget;
    uint32_t recent_window;
    uint32_t obs_window;
    uint32_t pool_kernel;
    size_t slots;

    (void)rng; /* entirely deterministic */

    budget = (config == NULL) ? 512u : config->budget;
    recent_window = (config == NULL) ? 32u : config->recent_window;
    obs_window = (config == NULL) ? 16u : config->obs_window;
    pool_kernel = (config == NULL) ? 7u : config->pool_kernel;

    /* The slot arrays hold budget + 1 entries; UINT32_MAX would wrap that. */
    if (budget == 0u || budget == UINT32_MAX || obs_window == 0u) {
        return NULL;
    }
    if (recent_window >= budget) {
        return NULL;
    }
    /* An even kernel has no centre, so the neighbourhood would be lopsided. */
    if (pool_kernel == 0u || (pool_kernel % 2u) == 0u) {
        return NULL;
    }

    self = (pb_kvcache_snapkv_state *)pb_alloc(allocator, sizeof(pb_kvcache_snapkv_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->recent_window = recent_window;
    self->obs_window = obs_window;
    self->pool_radius = (pool_kernel - 1u) / 2u;
    self->capacity = budget + 1u;
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
        snapkv_release(self);
        return NULL;
    }

    /* Position 0's token exists before the first decode step (see kv_cache.h),
     * with an empty history. */
    memset(self->history, 0, (size_t)obs_window * sizeof(float));
    self->positions[0] = 0u;
    self->doomed[0] = 0u;
    self->size = 1u;
    return (pb_kvcache *)self;
}

static void snapkv_destroy(pb_kvcache *policy)
{
    snapkv_release((pb_kvcache_snapkv_state *)policy);
}

static void snapkv_on_decode_step(pb_kvcache *policy, uint32_t pos, const float *attn,
                                  size_t attn_len)
{
    pb_kvcache_snapkv_state *self = (pb_kvcache_snapkv_state *)policy;
    size_t shared;
    size_t i;

    assert(self != NULL);

    /* A null attention vector is entirely inert — nothing is written and the
     * ring does not advance — so the window spans the last obs_window
     * *observed* steps rather than the last obs_window calls. Advancing without
     * writing would leave a stale weight in the slot for another full cycle, so
     * a window claiming to cover the recent past would quietly sum values of
     * indeterminate age. */
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

static size_t snapkv_evict(pb_kvcache *policy, uint32_t budget, uint32_t *victims,
                           size_t capacity)
{
    pb_kvcache_snapkv_state *self = (pb_kvcache_snapkv_state *)policy;
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

    if (self->size <= budget) {
        return 0;
    }
    needed = self->size - budget;

    protected_count = (self->recent_window < self->size) ? self->recent_window : self->size;
    evictable_end = self->size - protected_count;
    if (needed > evictable_end) {
        needed = evictable_end;
    }

    /* Refusing outright rather than evicting as much as fits: a partial
     * eviction would leave the caller over budget with no way to tell that the
     * buffer, not the policy, was the limit. */
    if ((size_t)needed > capacity) {
        return 0;
    }

    /* Sum each position's window from scratch rather than maintaining a running
     * total, which would drift — and the drift would have to be bit-identical
     * in three languages to stay reproducible. Slot order is fixed, index 0
     * upward rather than chronological: arbitrary but pinned. */
    for (i = 0u; i < self->size; ++i) {
        const float *record = &self->history[(size_t)i * (size_t)self->obs_window];
        double total = 0.0;
        uint32_t s;
        for (s = 0u; s < self->obs_window; ++s) {
            total += (double)record[s];
        }
        self->sums[i] = total;
    }

    /* Max-pool across neighbours, over the whole kept set: a protected recent
     * position is still a legitimate neighbour to inherit a score from. */
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
        uint32_t best = evictable_end; /* sentinel: nothing chosen yet */
        for (i = 0u; i < evictable_end; ++i) {
            if (self->doomed[i] != 0u) {
                continue;
            }
            /* Strictly less, so a tie leaves the earlier index standing — the
             * lower position, since the array is ascending. */
            if (best == evictable_end || self->pooled[i] < self->pooled[best]) {
                best = i;
            }
        }
        if (best == evictable_end) {
            break;
        }
        self->doomed[best] = 1u;
    }

    /* One compacting pass: victims come out in ascending position order, and
     * each survivor's whole history moves with it. */
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

static size_t snapkv_memory_bytes(const pb_kvcache *policy)
{
    const pb_kvcache_snapkv_state *self = (const pb_kvcache_snapkv_state *)policy;
    size_t slots;

    if (self == NULL) {
        return sizeof(pb_kvcache_snapkv_state);
    }
    slots = (size_t)self->capacity;
    return sizeof(pb_kvcache_snapkv_state) + slots * sizeof(uint32_t) +
           slots * (size_t)self->obs_window * sizeof(float) + slots * sizeof(double) +
           slots * sizeof(double) + slots * sizeof(uint8_t);
}

const pb_kvcache_vtable pb_kvcache_snapkv = { snapkv_create, snapkv_on_decode_step,
                                              snapkv_evict, snapkv_memory_bytes,
                                              snapkv_destroy };
