/*
 * GENERATED COPY — do not edit. Edit policies/cache/fifo/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * FIFO — evict the key that arrived first.
 *
 * Mirrors index.ts and policy.py. A circular buffer of keys and two indices;
 * hits do nothing at all.
 */

#include "policybook/cache/fifo.h"

#include <assert.h>
#include <stddef.h>

#include "policybook/allocator.h"

typedef struct pb_cache_fifo_state {
    const pb_allocator *allocator;
    uint64_t *slots; /* resident keys in arrival order */
    uint32_t capacity;
    uint32_t slot_count; /* capacity + 1: a caller inserts before evicting */
    uint32_t head;
    uint32_t length;
} pb_cache_fifo_state;

static pb_cache *fifo_create(const void *params, const pb_allocator *allocator, pb_rng *rng)
{
    const pb_cache_fifo_params *config = (const pb_cache_fifo_params *)params;
    pb_cache_fifo_state *self;
    uint32_t capacity;
    uint32_t slot_count;

    (void)rng; /* FIFO makes no random choices */

    capacity = (config == NULL) ? 1000u : config->capacity;
    if (capacity == 0u || capacity == UINT32_MAX) {
        return NULL;
    }
    slot_count = capacity + 1u;

    self = (pb_cache_fifo_state *)pb_alloc(allocator, sizeof(pb_cache_fifo_state));
    if (self == NULL) {
        return NULL;
    }

    self->slots = (uint64_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint64_t));
    if (self->slots == NULL) {
        pb_free(allocator, self, sizeof(pb_cache_fifo_state));
        return NULL;
    }

    self->allocator = allocator;
    self->capacity = capacity;
    self->slot_count = slot_count;
    self->head = 0;
    self->length = 0;
    return (pb_cache *)self;
}

static void fifo_destroy(pb_cache *cache)
{
    pb_cache_fifo_state *self = (pb_cache_fifo_state *)cache;
    const pb_allocator *allocator;

    if (self == NULL) {
        return;
    }
    allocator = self->allocator;
    pb_free(allocator, self->slots, (size_t)self->slot_count * sizeof(uint64_t));
    pb_free(allocator, self, sizeof(pb_cache_fifo_state));
}

static void fifo_on_access(pb_cache *cache, uint64_t key, bool hit, const pb_cache_meta *meta)
{
    pb_cache_fifo_state *self = (pb_cache_fifo_state *)cache;
    uint32_t index;

    (void)meta;
    assert(self != NULL);

    /* The whole policy: a hit changes nothing. FIFO does not learn. */
    if (hit) {
        return;
    }

    /* The caller must evict once over capacity; ignoring that would silently
     * overwrite a live entry. */
    assert(self->length < self->slot_count);
    if (self->length >= self->slot_count) {
        return;
    }

    index = self->head + self->length;
    if (index >= self->slot_count) {
        index -= self->slot_count;
    }
    self->slots[index] = key;
    self->length += 1u;
}

static uint64_t fifo_evict(pb_cache *cache)
{
    pb_cache_fifo_state *self = (pb_cache_fifo_state *)cache;
    uint64_t key;

    assert(self != NULL);
    assert(self->length > 0u);
    if (self->length == 0u) {
        return 0u;
    }

    key = self->slots[self->head];
    self->head += 1u;
    if (self->head >= self->slot_count) {
        self->head = 0;
    }
    self->length -= 1u;
    return key;
}

static size_t fifo_memory_bytes(const pb_cache *cache)
{
    const pb_cache_fifo_state *self = (const pb_cache_fifo_state *)cache;

    if (self == NULL) {
        return 0;
    }
    return sizeof(pb_cache_fifo_state) + (size_t)self->slot_count * sizeof(uint64_t);
}

const pb_cache_vtable pb_cache_fifo = {
    fifo_create,
    fifo_on_access,
    fifo_evict,
    NULL, /* admits everything */
    fifo_destroy,
    fifo_memory_bytes,
    false, /* allocates_after_create */
    "cache/fifo"
};
