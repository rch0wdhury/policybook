/*
 * GENERATED COPY — do not edit. Edit policies/cache/lru/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * LRU — evict the key used longest ago.
 *
 * Mirrors index.ts and policy.py: a map from key to slot, and an index-based
 * doubly linked list giving recency order. Slots are recycled through a free
 * stack, so nothing allocates after create.
 */

#include "policybook/cache/lru.h"

#include <assert.h>
#include <stddef.h>

#include "policybook/allocator.h"
#include "policybook/ds/ilist.h"
#include "policybook/ds/map.h"

typedef struct pb_cache_lru_state {
    const pb_allocator *allocator;
    uint32_t capacity;
    uint32_t slot_count; /* capacity + 1: a caller inserts before evicting */

    pb_map index;    /* key -> slot */
    pb_ilist recency; /* head is most recently used, tail is the victim */
    uint64_t *keys;  /* slot -> key */

    uint32_t *free_slots;
    uint32_t free_count;
} pb_cache_lru_state;

static pb_cache *lru_create(const void *params, const pb_allocator *allocator, pb_rng *rng)
{
    const pb_cache_lru_params *config = (const pb_cache_lru_params *)params;
    pb_cache_lru_state *self;
    uint32_t capacity;
    uint32_t slot_count;
    uint32_t slot;

    (void)rng; /* LRU makes no random choices */

    capacity = (config == NULL) ? 1000u : config->capacity;
    if (capacity == 0u || capacity == UINT32_MAX) {
        return NULL;
    }
    slot_count = capacity + 1u;

    self = (pb_cache_lru_state *)pb_alloc(allocator, sizeof(pb_cache_lru_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->capacity = capacity;
    self->slot_count = slot_count;
    self->free_count = 0;
    self->keys = NULL;
    self->free_slots = NULL;
    self->index.capacity = 0;
    self->recency.capacity = 0;

    self->keys = (uint64_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint64_t));
    self->free_slots = (uint32_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint32_t));
    if (self->keys == NULL || self->free_slots == NULL ||
        !pb_map_init(&self->index, slot_count, allocator) ||
        !pb_ilist_init(&self->recency, slot_count, allocator)) {
        /* destroy tolerates partially built state */
        pb_map_destroy(&self->index, allocator);
        pb_ilist_destroy(&self->recency, allocator);
        pb_free(allocator, self->keys, (size_t)slot_count * sizeof(uint64_t));
        pb_free(allocator, self->free_slots, (size_t)slot_count * sizeof(uint32_t));
        pb_free(allocator, self, sizeof(pb_cache_lru_state));
        return NULL;
    }

    for (slot = 0; slot < slot_count; ++slot) {
        self->free_slots[slot] = slot_count - 1u - slot;
    }
    self->free_count = slot_count;

    return (pb_cache *)self;
}

static void lru_destroy(pb_cache *cache)
{
    pb_cache_lru_state *self = (pb_cache_lru_state *)cache;
    const pb_allocator *allocator;

    if (self == NULL) {
        return;
    }
    allocator = self->allocator;
    pb_map_destroy(&self->index, allocator);
    pb_ilist_destroy(&self->recency, allocator);
    pb_free(allocator, self->keys, (size_t)self->slot_count * sizeof(uint64_t));
    pb_free(allocator, self->free_slots, (size_t)self->slot_count * sizeof(uint32_t));
    pb_free(allocator, self, sizeof(pb_cache_lru_state));
}

static void lru_on_access(pb_cache *cache, uint64_t key, bool hit, const pb_cache_meta *meta)
{
    pb_cache_lru_state *self = (pb_cache_lru_state *)cache;
    uint32_t slot;

    (void)meta;
    assert(self != NULL);

    if (hit) {
        /* A caller reporting a hit for an absent key has a residency bug. */
        if (!pb_map_get(&self->index, key, &slot)) {
            assert(false);
            return;
        }
        pb_ilist_move_to_front(&self->recency, slot);
        return;
    }

    assert(self->free_count > 0u);
    if (self->free_count == 0u) {
        return;
    }

    self->free_count -= 1u;
    slot = self->free_slots[self->free_count];
    self->keys[slot] = key;
    (void)pb_map_put(&self->index, key, slot);
    pb_ilist_push_front(&self->recency, slot);
}

static uint64_t lru_evict(pb_cache *cache)
{
    pb_cache_lru_state *self = (pb_cache_lru_state *)cache;
    uint32_t slot;
    uint64_t key;

    assert(self != NULL);

    slot = pb_ilist_pop_back(&self->recency);
    assert(slot != PB_ILIST_NIL);
    if (slot == PB_ILIST_NIL) {
        return 0u;
    }

    key = self->keys[slot];
    (void)pb_map_remove(&self->index, key);
    self->free_slots[self->free_count] = slot;
    self->free_count += 1u;
    return key;
}

static size_t lru_memory_bytes(const pb_cache *cache)
{
    const pb_cache_lru_state *self = (const pb_cache_lru_state *)cache;

    if (self == NULL) {
        return 0;
    }
    return sizeof(pb_cache_lru_state) +
           (size_t)self->slot_count * (sizeof(uint64_t) + sizeof(uint32_t)) +
           pb_map_memory_bytes(&self->index) + pb_ilist_memory_bytes(&self->recency);
}

const pb_cache_vtable pb_cache_lru = {
    lru_create,
    lru_on_access,
    lru_evict,
    NULL, /* admits everything */
    lru_destroy,
    lru_memory_bytes,
    false, /* allocates_after_create */
    "cache/lru"
};
