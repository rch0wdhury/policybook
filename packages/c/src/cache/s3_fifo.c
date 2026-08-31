/*
 * GENERATED COPY — do not edit. Edit policies/cache/s3-fifo/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * S3-FIFO — three FIFO queues, two bits per entry, no list surgery.
 *
 * Mirrors index.ts and policy.py, including the one adaptation from the paper
 * documented in README.md: eviction returns exactly one victim per call.
 */

#include "policybook/cache/s3_fifo.h"

#include <assert.h>
#include <stddef.h>

#include "policybook/allocator.h"
#include "policybook/ds/ilist.h"
#include "policybook/ds/map.h"
#include "policybook/ds/ring.h"

/* Two bits per entry: three second chances and no more. */
#define PB_S3_MAX_FREQUENCY 3u
/* An entry must have been requested more than once in S to be promoted. */
#define PB_S3_PROMOTION_THRESHOLD 1u

#define PB_S3_SMALL 0u
#define PB_S3_MAIN 1u

typedef struct pb_cache_s3_fifo_state {
    const pb_allocator *allocator;
    uint32_t capacity;
    uint32_t slot_count;
    uint32_t small_size;
    uint32_t main_size;

    pb_map index; /* resident key -> slot */
    uint64_t *keys;
    uint8_t *frequency;
    uint8_t *queue_of;

    pb_ring small;
    pb_ring main;

    /* G: keys of objects that fell out of S, newest at the list head. A list,
     * not a ring, because promotion removes a ghost from the middle and the
     * remaining ghosts keep their order and their claim on G's capacity. */
    uint64_t *ghost_keys;   /* ghost slot -> key */
    pb_ilist ghost_list;    /* order; the tail is the oldest ghost */
    pb_map ghost_index;     /* ghost key -> its slot */
    uint32_t *ghost_free;   /* unused ghost slots */
    uint32_t ghost_free_count;

    uint32_t *free_slots;
    uint32_t free_count;
} pb_cache_s3_fifo_state;

static void pb_s3_release(pb_cache_s3_fifo_state *self)
{
    const pb_allocator *allocator = self->allocator;
    size_t slots = (size_t)self->slot_count;

    pb_map_destroy(&self->index, allocator);
    pb_map_destroy(&self->ghost_index, allocator);
    pb_ring_destroy(&self->small, allocator);
    pb_ring_destroy(&self->main, allocator);
    pb_ilist_destroy(&self->ghost_list, allocator);
    pb_free(allocator, self->keys, slots * sizeof(uint64_t));
    pb_free(allocator, self->frequency, slots * sizeof(uint8_t));
    pb_free(allocator, self->queue_of, slots * sizeof(uint8_t));
    pb_free(allocator, self->free_slots, slots * sizeof(uint32_t));
    pb_free(allocator, self->ghost_keys, (size_t)self->main_size * sizeof(uint64_t));
    pb_free(allocator, self->ghost_free, (size_t)self->main_size * sizeof(uint32_t));
    pb_free(allocator, self, sizeof(pb_cache_s3_fifo_state));
}

static pb_cache *s3_create(const void *params, const pb_allocator *allocator, pb_rng *rng)
{
    const pb_cache_s3_fifo_params *config = (const pb_cache_s3_fifo_params *)params;
    pb_cache_s3_fifo_state *self;
    uint32_t capacity;
    uint32_t slot_count;
    uint32_t slot;
    double small_fraction;

    (void)rng; /* S3-FIFO makes no random choices */

    capacity = (config == NULL) ? 1000u : config->capacity;
    small_fraction = (config == NULL) ? 0.1 : config->small_fraction;

    /* Capacity 1 has no room for both a small and a main queue. */
    if (capacity < 2u || capacity == UINT32_MAX) {
        return NULL;
    }
    if (!(small_fraction > 0.0) || small_fraction >= 1.0) {
        return NULL;
    }

    slot_count = capacity + 1u;

    self = (pb_cache_s3_fifo_state *)pb_alloc(allocator, sizeof(pb_cache_s3_fifo_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->capacity = capacity;
    self->slot_count = slot_count;
    self->index.capacity = 0;
    self->ghost_index.capacity = 0;
    self->small.capacity = 0;
    self->main.capacity = 0;
    self->ghost_list.capacity = 0;
    self->ghost_free_count = 0;
    self->free_count = 0;

    self->small_size = (uint32_t)((double)capacity * small_fraction);
    if (self->small_size == 0u) {
        self->small_size = 1u;
    }
    self->main_size = capacity - self->small_size;

    self->keys = (uint64_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint64_t));
    self->frequency = (uint8_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint8_t));
    self->queue_of = (uint8_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint8_t));
    self->free_slots = (uint32_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint32_t));
    self->ghost_keys =
        (uint64_t *)pb_alloc(allocator, (size_t)self->main_size * sizeof(uint64_t));
    self->ghost_free =
        (uint32_t *)pb_alloc(allocator, (size_t)self->main_size * sizeof(uint32_t));

    if (self->keys == NULL || self->frequency == NULL || self->queue_of == NULL ||
        self->free_slots == NULL || self->ghost_keys == NULL || self->ghost_free == NULL ||
        !pb_map_init(&self->index, slot_count, allocator) ||
        !pb_map_init(&self->ghost_index, self->main_size, allocator) ||
        !pb_ring_init(&self->small, slot_count, allocator) ||
        !pb_ring_init(&self->main, slot_count, allocator) ||
        !pb_ilist_init(&self->ghost_list, self->main_size, allocator)) {
        pb_s3_release(self);
        return NULL;
    }

    for (slot = 0; slot < slot_count; ++slot) {
        self->frequency[slot] = 0u;
        self->queue_of[slot] = (uint8_t)PB_S3_SMALL;
        self->free_slots[slot] = slot_count - 1u - slot;
    }
    self->free_count = slot_count;
    for (slot = 0; slot < self->main_size; ++slot) {
        self->ghost_free[slot] = self->main_size - 1u - slot;
    }
    self->ghost_free_count = self->main_size;

    return (pb_cache *)self;
}

static void s3_destroy(pb_cache *cache)
{
    pb_cache_s3_fifo_state *self = (pb_cache_s3_fifo_state *)cache;

    if (self == NULL) {
        return;
    }
    pb_s3_release(self);
}

static void pb_s3_remember_ghost(pb_cache_s3_fifo_state *self, uint64_t key)
{
    uint32_t ghost_slot;

    if (self->ghost_list.length == self->main_size) {
        /* Full: the oldest identifier is forgotten. */
        ghost_slot = pb_ilist_pop_back(&self->ghost_list);
        (void)pb_map_remove(&self->ghost_index, self->ghost_keys[ghost_slot]);
    } else {
        self->ghost_free_count -= 1u;
        ghost_slot = self->ghost_free[self->ghost_free_count];
    }

    self->ghost_keys[ghost_slot] = key;
    pb_ilist_push_front(&self->ghost_list, ghost_slot);
    (void)pb_map_put(&self->ghost_index, key, ghost_slot);
}

static uint64_t pb_s3_release_slot(pb_cache_s3_fifo_state *self, uint32_t slot)
{
    uint64_t key = self->keys[slot];

    (void)pb_map_remove(&self->index, key);
    self->frequency[slot] = 0u;
    self->free_slots[self->free_count] = slot;
    self->free_count += 1u;
    return key;
}

static void s3_on_access(pb_cache *cache, uint64_t key, bool hit, const pb_cache_meta *meta)
{
    pb_cache_s3_fifo_state *self = (pb_cache_s3_fifo_state *)cache;
    uint32_t slot;
    uint32_t ghost_slot;

    (void)meta;
    assert(self != NULL);

    if (hit) {
        if (!pb_map_get(&self->index, key, &slot)) {
            assert(false); /* the caller's residency tracking is wrong */
            return;
        }
        /* The entire hit path: bump a two-bit counter. Nothing moves. */
        if (self->frequency[slot] < PB_S3_MAX_FREQUENCY) {
            self->frequency[slot] = (uint8_t)(self->frequency[slot] + 1u);
        }
        return;
    }

    assert(self->free_count > 0u);
    if (self->free_count == 0u) {
        return;
    }

    self->free_count -= 1u;
    slot = self->free_slots[self->free_count];
    self->keys[slot] = key;
    self->frequency[slot] = 0u;
    (void)pb_map_put(&self->index, key, slot);

    if (pb_map_get(&self->ghost_index, key, &ghost_slot)) {
        /* Falling out of the small queue and coming back is itself evidence of
         * reuse, so the key skips the audition. Promotion removes exactly this
         * ghost; the others keep their order and their claim on G's capacity. */
        (void)pb_map_remove(&self->ghost_index, key);
        pb_ilist_remove(&self->ghost_list, ghost_slot);
        self->ghost_free[self->ghost_free_count] = ghost_slot;
        self->ghost_free_count += 1u;
        self->queue_of[slot] = (uint8_t)PB_S3_MAIN;
        (void)pb_ring_push_back(&self->main, slot);
        return;
    }

    self->queue_of[slot] = (uint8_t)PB_S3_SMALL;
    (void)pb_ring_push_back(&self->small, slot);
}

/*
 * Drain the small queue until something leaves the cache.
 *
 * Returns true and sets `victim` when an entry was evicted; false if the small
 * queue emptied by promotion without evicting anything.
 */
static bool pb_s3_evict_from_small(pb_cache_s3_fifo_state *self, uint64_t *victim)
{
    while (!pb_ring_is_empty(&self->small)) {
        uint32_t slot = pb_ring_pop_front(&self->small);

        if (self->frequency[slot] > PB_S3_PROMOTION_THRESHOLD) {
            self->queue_of[slot] = (uint8_t)PB_S3_MAIN;
            (void)pb_ring_push_back(&self->main, slot);
            continue;
        }

        pb_s3_remember_ghost(self, self->keys[slot]);
        *victim = pb_s3_release_slot(self, slot);
        return true;
    }
    return false;
}

/*
 * Take from the main queue, spending each entry's remaining second chances.
 *
 * Terminates because every pass decrements a counter that a later pass cannot
 * spend again.
 */
static bool pb_s3_evict_from_main(pb_cache_s3_fifo_state *self, uint64_t *victim)
{
    while (!pb_ring_is_empty(&self->main)) {
        uint32_t slot = pb_ring_pop_front(&self->main);

        if (self->frequency[slot] > 0u) {
            self->frequency[slot] = (uint8_t)(self->frequency[slot] - 1u);
            (void)pb_ring_push_back(&self->main, slot);
            continue;
        }

        *victim = pb_s3_release_slot(self, slot);
        return true;
    }
    return false;
}

static uint64_t s3_evict(pb_cache *cache)
{
    pb_cache_s3_fifo_state *self = (pb_cache_s3_fifo_state *)cache;
    uint64_t victim = 0u;

    assert(self != NULL);

    /* The small queue is drained while it is over its share, so an object that
     * has not proven reuse is always the better victim. */
    if (self->small.length >= self->small_size && pb_s3_evict_from_small(self, &victim)) {
        return victim;
    }
    if (pb_s3_evict_from_main(self, &victim)) {
        return victim;
    }
    /* The main queue is empty; fall back to the small one however short. */
    if (pb_s3_evict_from_small(self, &victim)) {
        return victim;
    }

    assert(false); /* nothing resident */
    return 0u;
}

static size_t s3_memory_bytes(const pb_cache *cache)
{
    const pb_cache_s3_fifo_state *self = (const pb_cache_s3_fifo_state *)cache;

    if (self == NULL) {
        return 0;
    }
    return sizeof(pb_cache_s3_fifo_state) +
           (size_t)self->slot_count *
               (sizeof(uint64_t) + 2u * sizeof(uint8_t) + sizeof(uint32_t)) +
           (size_t)self->main_size * (sizeof(uint64_t) + sizeof(uint32_t)) +
           pb_map_memory_bytes(&self->index) + pb_map_memory_bytes(&self->ghost_index) +
           pb_ring_memory_bytes(&self->small) + pb_ring_memory_bytes(&self->main) +
           pb_ilist_memory_bytes(&self->ghost_list);
}

const pb_cache_vtable pb_cache_s3_fifo = {
    s3_create,
    s3_on_access,
    s3_evict,
    NULL, /* admission is decided by the small queue, not at insertion */
    s3_destroy,
    s3_memory_bytes,
    false, /* allocates_after_create */
    "cache/s3-fifo"
};
