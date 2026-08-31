/*
 * CLOCK — approximate LRU with one reference bit per entry.
 *
 * Mirrors index.ts and policy.py: the queue formulation, which is the same
 * algorithm as the circular buffer with a rotating hand. The ring's front is
 * the hand.
 */

#include "policybook/cache/clock.h"

#include <assert.h>
#include <stddef.h>

#include "policybook/allocator.h"
#include "policybook/ds/map.h"
#include "policybook/ds/ring.h"

typedef struct pb_cache_clock_state {
    const pb_allocator *allocator;
    uint32_t capacity;
    uint32_t slot_count; /* capacity + 1: a caller inserts before evicting */

    pb_map index;      /* key -> slot */
    pb_ring order;     /* slot indices in arrival order; the front is the hand */
    uint64_t *keys;    /* slot -> key */
    uint8_t *referenced; /* slot -> reference bit */

    uint32_t *free_slots;
    uint32_t free_count;
} pb_cache_clock_state;

static void clock_release(pb_cache_clock_state *self)
{
    const pb_allocator *allocator = self->allocator;
    size_t slots = (size_t)self->slot_count;

    pb_map_destroy(&self->index, allocator);
    pb_ring_destroy(&self->order, allocator);
    pb_free(allocator, self->keys, slots * sizeof(uint64_t));
    pb_free(allocator, self->referenced, slots * sizeof(uint8_t));
    pb_free(allocator, self->free_slots, slots * sizeof(uint32_t));
    pb_free(allocator, self, sizeof(pb_cache_clock_state));
}

static pb_cache *clock_create(const void *params, const pb_allocator *allocator, pb_rng *rng)
{
    const pb_cache_clock_params *config = (const pb_cache_clock_params *)params;
    pb_cache_clock_state *self;
    uint32_t capacity;
    uint32_t slot_count;
    uint32_t slot;

    (void)rng; /* CLOCK makes no random choices */

    capacity = (config == NULL) ? 1000u : config->capacity;
    if (capacity == 0u || capacity == UINT32_MAX) {
        return NULL;
    }
    slot_count = capacity + 1u;

    self = (pb_cache_clock_state *)pb_alloc(allocator, sizeof(pb_cache_clock_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->capacity = capacity;
    self->slot_count = slot_count;
    self->free_count = 0;
    self->index.capacity = 0;
    self->order.capacity = 0;
    self->keys = (uint64_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint64_t));
    self->referenced = (uint8_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint8_t));
    self->free_slots = (uint32_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint32_t));

    if (self->keys == NULL || self->referenced == NULL || self->free_slots == NULL ||
        !pb_map_init(&self->index, slot_count, allocator) ||
        !pb_ring_init(&self->order, slot_count, allocator)) {
        clock_release(self);
        return NULL;
    }

    for (slot = 0; slot < slot_count; ++slot) {
        self->referenced[slot] = 0u;
        self->free_slots[slot] = slot_count - 1u - slot;
    }
    self->free_count = slot_count;

    return (pb_cache *)self;
}

static void clock_destroy(pb_cache *cache)
{
    pb_cache_clock_state *self = (pb_cache_clock_state *)cache;

    if (self == NULL) {
        return;
    }
    clock_release(self);
}

static void clock_on_access(pb_cache *cache, uint64_t key, bool hit, const pb_cache_meta *meta)
{
    pb_cache_clock_state *self = (pb_cache_clock_state *)cache;
    uint32_t slot;

    (void)meta;
    assert(self != NULL);

    if (hit) {
        if (!pb_map_get(&self->index, key, &slot)) {
            assert(false); /* the caller's residency tracking is wrong */
            return;
        }
        /* The entire hit path: set one bit. No reordering, no shared writes. */
        self->referenced[slot] = 1u;
        return;
    }

    assert(self->free_count > 0u);
    if (self->free_count == 0u) {
        return;
    }

    self->free_count -= 1u;
    slot = self->free_slots[self->free_count];
    self->keys[slot] = key;
    self->referenced[slot] = 0u;
    (void)pb_map_put(&self->index, key, slot);
    (void)pb_ring_push_back(&self->order, slot);
}

static uint64_t clock_evict(pb_cache *cache)
{
    pb_cache_clock_state *self = (pb_cache_clock_state *)cache;

    assert(self != NULL);
    assert(!pb_ring_is_empty(&self->order));

    /* The hand can pass each entry at most once, because passing it clears
     * that entry's bit. */
    for (;;) {
        uint32_t slot = pb_ring_pop_front(&self->order);
        uint64_t key;

        if (slot == PB_RING_NIL) {
            return 0u;
        }

        if (self->referenced[slot] != 0u) {
            self->referenced[slot] = 0u; /* second chance */
            (void)pb_ring_push_back(&self->order, slot);
            continue;
        }

        key = self->keys[slot];
        (void)pb_map_remove(&self->index, key);
        self->free_slots[self->free_count] = slot;
        self->free_count += 1u;
        return key;
    }
}

static size_t clock_memory_bytes(const pb_cache *cache)
{
    const pb_cache_clock_state *self = (const pb_cache_clock_state *)cache;

    if (self == NULL) {
        return 0;
    }
    return sizeof(pb_cache_clock_state) +
           (size_t)self->slot_count * (sizeof(uint64_t) + sizeof(uint8_t) + sizeof(uint32_t)) +
           pb_map_memory_bytes(&self->index) + pb_ring_memory_bytes(&self->order);
}

const pb_cache_vtable pb_cache_clock = {
    clock_create,
    clock_on_access,
    clock_evict,
    NULL, /* admits everything */
    clock_destroy,
    clock_memory_bytes,
    false, /* allocates_after_create */
    "cache/clock"
};
