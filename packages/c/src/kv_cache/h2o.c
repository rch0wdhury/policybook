/*
 * GENERATED COPY — do not edit. Edit policies/kv-cache/h2o/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * H2O — keep the tokens that have received the most attention so far.
 *
 * Mirrors index.ts and policy.py. Three parallel arrays indexed by kept order,
 * which is the order the attention arrives in, so no lookup is ever needed on
 * the decode path.
 */

#include "policybook/kv_cache/h2o.h"

#include <assert.h>
#include <stddef.h>

#include "policybook/allocator.h"

typedef struct pb_kvcache_h2o_state {
    const pb_allocator *allocator;
    uint32_t recent_window;
    uint32_t capacity; /* budget + 1 */
    uint32_t size;
    uint32_t *positions; /* kept positions, ascending */
    double *scores;      /* cumulative attention, parallel to positions */
    uint8_t *doomed;     /* eviction scratch, so evict allocates nothing */
} pb_kvcache_h2o_state;

static void h2o_release(pb_kvcache_h2o_state *self)
{
    size_t slots;

    if (self == NULL) {
        return;
    }
    slots = (size_t)self->capacity;
    pb_free(self->allocator, self->positions, slots * sizeof(uint32_t));
    pb_free(self->allocator, self->scores, slots * sizeof(double));
    pb_free(self->allocator, self->doomed, slots * sizeof(uint8_t));
    pb_free(self->allocator, self, sizeof(pb_kvcache_h2o_state));
}

static pb_kvcache *h2o_create(const void *params, const pb_allocator *allocator, pb_rng *rng)
{
    const pb_kvcache_h2o_params *config = (const pb_kvcache_h2o_params *)params;
    pb_kvcache_h2o_state *self;
    uint32_t budget;
    uint32_t recent_window;
    size_t slots;

    (void)rng; /* entirely deterministic */

    budget = (config == NULL) ? 512u : config->budget;
    recent_window = (config == NULL) ? 32u : config->recent_window;
    /* The slot arrays hold budget + 1 entries; UINT32_MAX would wrap that. */
    if (budget == 0u || budget == UINT32_MAX) {
        return NULL;
    }
    /* With the whole budget protected there would be nothing left to evict. */
    if (recent_window >= budget) {
        return NULL;
    }

    self = (pb_kvcache_h2o_state *)pb_alloc(allocator, sizeof(pb_kvcache_h2o_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->recent_window = recent_window;
    self->capacity = budget + 1u;
    self->size = 0u;
    slots = (size_t)self->capacity;

    self->positions = (uint32_t *)pb_alloc(allocator, slots * sizeof(uint32_t));
    self->scores = (double *)pb_alloc(allocator, slots * sizeof(double));
    self->doomed = (uint8_t *)pb_alloc(allocator, slots * sizeof(uint8_t));
    if (self->positions == NULL || self->scores == NULL || self->doomed == NULL) {
        h2o_release(self);
        return NULL;
    }

    /* Position 0's token exists before the first decode step (see kv_cache.h),
     * and starts with no attention to its name. */
    self->positions[0] = 0u;
    self->scores[0] = 0.0;
    self->doomed[0] = 0u;
    self->size = 1u;
    return (pb_kvcache *)self;
}

static void h2o_destroy(pb_kvcache *policy)
{
    h2o_release((pb_kvcache_h2o_state *)policy);
}

static void h2o_on_decode_step(pb_kvcache *policy, uint32_t pos, const float *attn,
                               size_t attn_len)
{
    pb_kvcache_h2o_state *self = (pb_kvcache_h2o_state *)policy;
    size_t shared;
    size_t i;

    assert(self != NULL);

    /* attn[i] belongs to the i-th kept position in ascending order, which is
     * exactly how `positions` is stored — the two are index-aligned. */
    if (attn != NULL) {
        shared = (attn_len < (size_t)self->size) ? attn_len : (size_t)self->size;
        for (i = 0; i < shared; ++i) {
            self->scores[i] = self->scores[i] + (double)attn[i];
        }
    }

    /* Holding more than budget + 1 means the caller's budget does not match the
     * policy's, which is a configuration error rather than a decision to make. */
    assert(self->size < self->capacity);
    if (self->size >= self->capacity) {
        return;
    }

    self->positions[self->size] = pos;
    self->scores[self->size] = 0.0;
    self->doomed[self->size] = 0u;
    self->size += 1u;
}

static size_t h2o_evict(pb_kvcache *policy, uint32_t budget, uint32_t *victims, size_t capacity)
{
    pb_kvcache_h2o_state *self = (pb_kvcache_h2o_state *)policy;
    uint32_t protected_count;
    uint32_t evictable_end;
    uint32_t needed;
    uint32_t taken;
    uint32_t read;
    uint32_t write = 0u;
    size_t written = 0;

    assert(self != NULL);
    assert(victims != NULL);

    if (self->size <= budget) {
        return 0;
    }
    needed = self->size - budget;

    /* The recent window is the tail of the ascending array, so everything
     * before evictable_end is fair game and nothing after it is. */
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

    /* Repeated argmin rather than a sort: in steady state exactly one position
     * goes per step, so this is a single linear scan. */
    for (taken = 0u; taken < needed; ++taken) {
        uint32_t best = evictable_end; /* sentinel: nothing chosen yet */
        uint32_t i;
        for (i = 0u; i < evictable_end; ++i) {
            if (self->doomed[i] != 0u) {
                continue;
            }
            /* Strictly less, so a tie leaves the earlier index standing — the
             * lower position, since the array is ascending. */
            if (best == evictable_end || self->scores[i] < self->scores[best]) {
                best = i;
            }
        }
        if (best == evictable_end) {
            break;
        }
        self->doomed[best] = 1u;
    }

    /* One compacting pass: victims come out in ascending position order. */
    for (read = 0u; read < self->size; ++read) {
        if (self->doomed[read] != 0u) {
            self->doomed[read] = 0u;
            victims[written] = self->positions[read];
            written += 1;
            continue;
        }
        self->positions[write] = self->positions[read];
        self->scores[write] = self->scores[read];
        write += 1u;
    }
    self->size = write;
    return written;
}

static size_t h2o_memory_bytes(const pb_kvcache *policy)
{
    const pb_kvcache_h2o_state *self = (const pb_kvcache_h2o_state *)policy;
    size_t slots;

    if (self == NULL) {
        return sizeof(pb_kvcache_h2o_state);
    }
    slots = (size_t)self->capacity;
    return sizeof(pb_kvcache_h2o_state) + slots * sizeof(uint32_t) + slots * sizeof(double) +
           slots * sizeof(uint8_t);
}

const pb_kvcache_vtable pb_kvcache_h2o = { h2o_create, h2o_on_decode_step, h2o_evict,
                                           h2o_memory_bytes, h2o_destroy };
