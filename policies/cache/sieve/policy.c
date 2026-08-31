/*
 * SIEVE — a FIFO queue with a hand that gives each entry one chance.
 *
 * Mirrors index.ts and policy.py. Entries are held in an index-based doubly
 * linked list because the hand removes from the middle; `newer` and `older`
 * name the directions, which the paper's prev/next leave ambiguous when the
 * hand travels one way and insertion happens at the other end.
 */

#include "policybook/cache/sieve.h"

#include <assert.h>
#include <stddef.h>

#include "policybook/allocator.h"
#include "policybook/ds/map.h"

#define PB_SIEVE_NIL 0xFFFFFFFFu

typedef struct pb_cache_sieve_state {
    const pb_allocator *allocator;
    uint32_t capacity;
    uint32_t slot_count; /* capacity + 1: a caller inserts before evicting */

    pb_map index;   /* key -> slot */
    uint64_t *keys; /* slot -> key */
    uint8_t *visited;

    /* Insertion order. Nothing ever moves within it. */
    uint32_t *newer;
    uint32_t *older;
    uint32_t newest;
    uint32_t oldest;

    /*
     * Where the hand stopped, or PB_SIEVE_NIL to start from the oldest entry.
     * Retaining this across evictions is the algorithm, not an optimisation.
     */
    uint32_t hand;

    uint32_t *free_slots;
    uint32_t free_count;
} pb_cache_sieve_state;

static void sieve_release(pb_cache_sieve_state *self)
{
    const pb_allocator *allocator = self->allocator;
    size_t slots = (size_t)self->slot_count;

    pb_map_destroy(&self->index, allocator);
    pb_free(allocator, self->keys, slots * sizeof(uint64_t));
    pb_free(allocator, self->visited, slots * sizeof(uint8_t));
    pb_free(allocator, self->newer, slots * sizeof(uint32_t));
    pb_free(allocator, self->older, slots * sizeof(uint32_t));
    pb_free(allocator, self->free_slots, slots * sizeof(uint32_t));
    pb_free(allocator, self, sizeof(pb_cache_sieve_state));
}

static pb_cache *sieve_create(const void *params, const pb_allocator *allocator, pb_rng *rng)
{
    const pb_cache_sieve_params *config = (const pb_cache_sieve_params *)params;
    pb_cache_sieve_state *self;
    uint32_t capacity;
    uint32_t slot_count;
    uint32_t slot;

    (void)rng; /* SIEVE makes no random choices */

    capacity = (config == NULL) ? 1000u : config->capacity;
    if (capacity == 0u || capacity == UINT32_MAX) {
        return NULL;
    }
    slot_count = capacity + 1u;

    self = (pb_cache_sieve_state *)pb_alloc(allocator, sizeof(pb_cache_sieve_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->capacity = capacity;
    self->slot_count = slot_count;
    self->free_count = 0;
    self->index.capacity = 0;
    self->keys = (uint64_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint64_t));
    self->visited = (uint8_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint8_t));
    self->newer = (uint32_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint32_t));
    self->older = (uint32_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint32_t));
    self->free_slots = (uint32_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint32_t));

    if (self->keys == NULL || self->visited == NULL || self->newer == NULL ||
        self->older == NULL || self->free_slots == NULL ||
        !pb_map_init(&self->index, slot_count, allocator)) {
        sieve_release(self);
        return NULL;
    }

    for (slot = 0; slot < slot_count; ++slot) {
        self->visited[slot] = 0u;
        self->newer[slot] = PB_SIEVE_NIL;
        self->older[slot] = PB_SIEVE_NIL;
        self->free_slots[slot] = slot_count - 1u - slot;
    }
    self->free_count = slot_count;
    self->newest = PB_SIEVE_NIL;
    self->oldest = PB_SIEVE_NIL;
    self->hand = PB_SIEVE_NIL;

    return (pb_cache *)self;
}

static void sieve_destroy(pb_cache *cache)
{
    pb_cache_sieve_state *self = (pb_cache_sieve_state *)cache;

    if (self == NULL) {
        return;
    }
    sieve_release(self);
}

static void sieve_unlink(pb_cache_sieve_state *self, uint32_t slot)
{
    uint32_t newer = self->newer[slot];
    uint32_t older = self->older[slot];

    if (newer != PB_SIEVE_NIL) {
        self->older[newer] = older;
    } else {
        self->newest = older;
    }
    if (older != PB_SIEVE_NIL) {
        self->newer[older] = newer;
    } else {
        self->oldest = newer;
    }

    self->newer[slot] = PB_SIEVE_NIL;
    self->older[slot] = PB_SIEVE_NIL;
}

static void sieve_on_access(pb_cache *cache, uint64_t key, bool hit, const pb_cache_meta *meta)
{
    pb_cache_sieve_state *self = (pb_cache_sieve_state *)cache;
    uint32_t slot;

    (void)meta;
    assert(self != NULL);

    if (hit) {
        if (!pb_map_get(&self->index, key, &slot)) {
            assert(false); /* the caller's residency tracking is wrong */
            return;
        }
        /* The entire hit path: set one bit. The entry does not move. */
        self->visited[slot] = 1u;
        return;
    }

    assert(self->free_count > 0u);
    if (self->free_count == 0u) {
        return;
    }

    self->free_count -= 1u;
    slot = self->free_slots[self->free_count];
    self->keys[slot] = key;
    self->visited[slot] = 0u;
    (void)pb_map_put(&self->index, key, slot);

    /* New entries go at the new end, ahead of the hand. */
    self->newer[slot] = PB_SIEVE_NIL;
    self->older[slot] = self->newest;
    if (self->newest != PB_SIEVE_NIL) {
        self->newer[self->newest] = slot;
    } else {
        self->oldest = slot;
    }
    self->newest = slot;
}

static uint64_t sieve_evict(pb_cache *cache)
{
    pb_cache_sieve_state *self = (pb_cache_sieve_state *)cache;
    uint32_t slot;
    uint64_t key;

    assert(self != NULL);
    assert(self->oldest != PB_SIEVE_NIL);
    if (self->oldest == PB_SIEVE_NIL) {
        return 0u;
    }

    /* Resume where the hand stopped, or start at the oldest entry. */
    slot = (self->hand == PB_SIEVE_NIL) ? self->oldest : self->hand;

    while (self->visited[slot] != 0u) {
        uint32_t next = self->newer[slot];
        self->visited[slot] = 0u;
        /* Past the newest entry, the hand wraps to the oldest. */
        slot = (next == PB_SIEVE_NIL) ? self->oldest : next;
    }

    /* The hand stops just beyond the victim, and stays there. */
    self->hand = self->newer[slot];

    key = self->keys[slot];
    sieve_unlink(self, slot);
    (void)pb_map_remove(&self->index, key);
    self->free_slots[self->free_count] = slot;
    self->free_count += 1u;
    return key;
}

static size_t sieve_memory_bytes(const pb_cache *cache)
{
    const pb_cache_sieve_state *self = (const pb_cache_sieve_state *)cache;

    if (self == NULL) {
        return 0;
    }
    return sizeof(pb_cache_sieve_state) +
           (size_t)self->slot_count *
               (sizeof(uint64_t) + sizeof(uint8_t) + 3u * sizeof(uint32_t)) +
           pb_map_memory_bytes(&self->index);
}

const pb_cache_vtable pb_cache_sieve = {
    sieve_create,
    sieve_on_access,
    sieve_evict,
    NULL, /* admits everything */
    sieve_destroy,
    sieve_memory_bytes,
    false, /* allocates_after_create */
    "cache/sieve"
};
