/*
 * GENERATED COPY — do not edit. Edit policies/kv-cache/sliding-window/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * Sliding window — keep the most recent tokens and forget the rest.
 *
 * Mirrors index.ts and policy.py. A ring buffer of positions: O(1) per step,
 * nothing allocated after create, and exactly budget + 1 slots because that is
 * the most that can be held before an eviction is asked for.
 */

#include "policybook/kv_cache/sliding_window.h"

#include <assert.h>
#include <stddef.h>

#include "policybook/allocator.h"

typedef struct pb_kvcache_sliding_window_state {
    const pb_allocator *allocator;
    uint32_t capacity; /* budget + 1 */
    uint32_t *slots;   /* kept positions in arrival order, oldest at head */
    uint32_t head;
    uint32_t size;
} pb_kvcache_sliding_window_state;

static pb_kvcache *sliding_window_create(const void *params, const pb_allocator *allocator,
                                         pb_rng *rng)
{
    const pb_kvcache_sliding_window_params *config =
        (const pb_kvcache_sliding_window_params *)params;
    pb_kvcache_sliding_window_state *self;
    uint32_t budget;
    uint32_t capacity;

    (void)rng; /* entirely deterministic */

    budget = (config == NULL) ? 512u : config->budget;
    /* The ring holds budget + 1 slots; a budget of UINT32_MAX would wrap it. */
    if (budget == 0u || budget == UINT32_MAX) {
        return NULL;
    }
    capacity = budget + 1u;

    self = (pb_kvcache_sliding_window_state *)pb_alloc(
        allocator, sizeof(pb_kvcache_sliding_window_state));
    if (self == NULL) {
        return NULL;
    }

    self->slots = (uint32_t *)pb_alloc(allocator, (size_t)capacity * sizeof(uint32_t));
    if (self->slots == NULL) {
        pb_free(allocator, self, sizeof(pb_kvcache_sliding_window_state));
        return NULL;
    }

    self->allocator = allocator;
    self->capacity = capacity;
    self->head = 0u;
    /* Position 0's token exists before the first decode step, so the cache
     * holds it from the outset (see kv_cache.h). */
    self->slots[0] = 0u;
    self->size = 1u;
    return (pb_kvcache *)self;
}

static void sliding_window_destroy(pb_kvcache *policy)
{
    pb_kvcache_sliding_window_state *self = (pb_kvcache_sliding_window_state *)policy;

    if (self == NULL) {
        return;
    }
    pb_free(self->allocator, self->slots, (size_t)self->capacity * sizeof(uint32_t));
    pb_free(self->allocator, self, sizeof(pb_kvcache_sliding_window_state));
}

static void sliding_window_on_decode_step(pb_kvcache *policy, uint32_t pos, const float *attn,
                                          size_t attn_len)
{
    pb_kvcache_sliding_window_state *self = (pb_kvcache_sliding_window_state *)policy;
    uint32_t slot;

    assert(self != NULL);

    /* Not reading the attention is the point: this policy cannot be accused of
     * using information it does not have. */
    (void)attn;
    (void)attn_len;

    /* Holding more than budget + 1 means the caller's budget does not match the
     * policy's, which is a configuration error rather than a decision to make. */
    assert(self->size < self->capacity);
    if (self->size >= self->capacity) {
        return;
    }

    slot = self->head + self->size;
    if (slot >= self->capacity) {
        slot -= self->capacity;
    }
    self->slots[slot] = pos;
    self->size += 1u;
}

static size_t sliding_window_evict(pb_kvcache *policy, uint32_t budget, uint32_t *victims,
                                   size_t capacity)
{
    pb_kvcache_sliding_window_state *self = (pb_kvcache_sliding_window_state *)policy;
    size_t written = 0;

    assert(self != NULL);
    assert(victims != NULL);

    /* Refusing outright rather than evicting as much as fits: a partial
     * eviction would leave the caller over budget with no way to tell that the
     * buffer, not the policy, was the limit. */
    if (self->size <= budget) {
        return 0;
    }
    if ((size_t)(self->size - budget) > capacity) {
        return 0;
    }

    while (self->size > budget) {
        victims[written] = self->slots[self->head];
        written += 1;
        self->head += 1u;
        if (self->head == self->capacity) {
            self->head = 0u;
        }
        self->size -= 1u;
    }
    return written;
}

static size_t sliding_window_memory_bytes(const pb_kvcache *policy)
{
    const pb_kvcache_sliding_window_state *self =
        (const pb_kvcache_sliding_window_state *)policy;

    if (self == NULL) {
        return sizeof(pb_kvcache_sliding_window_state);
    }
    return sizeof(pb_kvcache_sliding_window_state) +
           (size_t)self->capacity * sizeof(uint32_t);
}

const pb_kvcache_vtable pb_kvcache_sliding_window = { sliding_window_create,
                                                      sliding_window_on_decode_step,
                                                      sliding_window_evict,
                                                      sliding_window_memory_bytes,
                                                      sliding_window_destroy };
