/*
 * TOVA — drop whichever token the model just stopped looking at.
 *
 * Mirrors index.ts and policy.py. Structurally h2o with assignment in place of
 * accumulation and no recent window, which is the whole of the difference.
 */

#include "policybook/kv_cache/tova.h"

#include <assert.h>
#include <stddef.h>

#include "policybook/allocator.h"

/* Marks a position that has never appeared in an attention vector. Attention
 * weights are non-negative, so a negative value is unambiguous. */
#define PB_KVCACHE_TOVA_UNOBSERVED (-1.0)

typedef struct pb_kvcache_tova_state {
    const pb_allocator *allocator;
    uint32_t capacity; /* budget + 1 */
    uint32_t size;
    uint32_t *positions; /* kept positions, ascending */
    double *last_attn;   /* latest attention, parallel to positions */
    uint8_t *doomed;     /* eviction scratch, so evict allocates nothing */
} pb_kvcache_tova_state;

static void tova_release(pb_kvcache_tova_state *self)
{
    size_t slots;

    if (self == NULL) {
        return;
    }
    slots = (size_t)self->capacity;
    pb_free(self->allocator, self->positions, slots * sizeof(uint32_t));
    pb_free(self->allocator, self->last_attn, slots * sizeof(double));
    pb_free(self->allocator, self->doomed, slots * sizeof(uint8_t));
    pb_free(self->allocator, self, sizeof(pb_kvcache_tova_state));
}

static pb_kvcache *tova_create(const void *params, const pb_allocator *allocator, pb_rng *rng)
{
    const pb_kvcache_tova_params *config = (const pb_kvcache_tova_params *)params;
    pb_kvcache_tova_state *self;
    uint32_t budget;
    size_t slots;

    (void)rng; /* entirely deterministic */

    budget = (config == NULL) ? 512u : config->budget;
    /* The slot arrays hold budget + 1 entries; UINT32_MAX would wrap that. */
    if (budget == 0u || budget == UINT32_MAX) {
        return NULL;
    }

    self = (pb_kvcache_tova_state *)pb_alloc(allocator, sizeof(pb_kvcache_tova_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->capacity = budget + 1u;
    self->size = 0u;
    slots = (size_t)self->capacity;

    self->positions = (uint32_t *)pb_alloc(allocator, slots * sizeof(uint32_t));
    self->last_attn = (double *)pb_alloc(allocator, slots * sizeof(double));
    self->doomed = (uint8_t *)pb_alloc(allocator, slots * sizeof(uint8_t));
    if (self->positions == NULL || self->last_attn == NULL || self->doomed == NULL) {
        tova_release(self);
        return NULL;
    }

    /* Position 0's token exists before the first decode step (see kv_cache.h)
     * and nothing has attended to it yet. */
    self->positions[0] = 0u;
    self->last_attn[0] = PB_KVCACHE_TOVA_UNOBSERVED;
    self->doomed[0] = 0u;
    self->size = 1u;
    return (pb_kvcache *)self;
}

static void tova_destroy(pb_kvcache *policy)
{
    tova_release((pb_kvcache_tova_state *)policy);
}

static void tova_on_decode_step(pb_kvcache *policy, uint32_t pos, const float *attn,
                                size_t attn_len)
{
    pb_kvcache_tova_state *self = (pb_kvcache_tova_state *)policy;
    size_t shared;
    size_t i;

    assert(self != NULL);

    /* Assignment, not accumulation: the previous value is discarded outright,
     * which is the entire policy. */
    if (attn != NULL) {
        shared = (attn_len < (size_t)self->size) ? attn_len : (size_t)self->size;
        for (i = 0; i < shared; ++i) {
            self->last_attn[i] = (double)attn[i];
        }
    }

    assert(self->size < self->capacity);
    if (self->size >= self->capacity) {
        return;
    }

    self->positions[self->size] = pos;
    self->last_attn[self->size] = PB_KVCACHE_TOVA_UNOBSERVED;
    self->doomed[self->size] = 0u;
    self->size += 1u;
}

static size_t tova_evict(pb_kvcache *policy, uint32_t budget, uint32_t *victims, size_t capacity)
{
    pb_kvcache_tova_state *self = (pb_kvcache_tova_state *)policy;
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

    /* Refusing outright rather than evicting as much as fits: a partial
     * eviction would leave the caller over budget with no way to tell that the
     * buffer, not the policy, was the limit. */
    if ((size_t)needed > capacity) {
        return 0;
    }

    for (taken = 0u; taken < needed; ++taken) {
        uint32_t best = self->size; /* sentinel: nothing chosen yet */
        uint32_t i;
        for (i = 0u; i < self->size; ++i) {
            if (self->doomed[i] != 0u) {
                continue;
            }
            /* An unobserved position is not a candidate: it has no weight to be
             * ranked on, and treating its absence as zero would evict every
             * token the step it was generated. */
            if (self->last_attn[i] == PB_KVCACHE_TOVA_UNOBSERVED) {
                continue;
            }
            /* Strictly less, so a tie leaves the earlier index standing — the
             * lower position, since the array is ascending. */
            if (best == self->size || self->last_attn[i] < self->last_attn[best]) {
                best = i;
            }
        }
        if (best == self->size) {
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
        self->last_attn[write] = self->last_attn[read];
        write += 1u;
    }
    self->size = write;
    return written;
}

static size_t tova_memory_bytes(const pb_kvcache *policy)
{
    const pb_kvcache_tova_state *self = (const pb_kvcache_tova_state *)policy;
    size_t slots;

    if (self == NULL) {
        return sizeof(pb_kvcache_tova_state);
    }
    slots = (size_t)self->capacity;
    return sizeof(pb_kvcache_tova_state) + slots * sizeof(uint32_t) + slots * sizeof(double) +
           slots * sizeof(uint8_t);
}

const pb_kvcache_vtable pb_kvcache_tova = { tova_create, tova_on_decode_step, tova_evict,
                                            tova_memory_bytes, tova_destroy };
