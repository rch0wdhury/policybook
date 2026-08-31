/*
 * GENERATED COPY — do not edit. Edit policies/cache/2q/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * 2Q — admit to the main cache only on a second access.
 *
 * Mirrors index.ts and policy.py. Three structures: A1in as a FIFO ring of
 * slots, Am as an index-based LRU list, and A1out as a FIFO list of keys with
 * a membership map — a list, not a ring, because promotion removes a ghost
 * from the middle and the remaining ghosts must keep both their order and
 * their claim on A1out's capacity. Everything is taken in create.
 */

#include "policybook/cache/2q.h"

#include <assert.h>
#include <stddef.h>

#include "policybook/allocator.h"
#include "policybook/ds/ilist.h"
#include "policybook/ds/map.h"
#include "policybook/ds/ring.h"

typedef struct pb_cache_2q_state {
    const pb_allocator *allocator;
    uint32_t capacity;
    uint32_t slot_count; /* capacity + 1: a caller inserts before evicting */
    uint32_t kin_size;
    uint32_t kout_size;

    pb_map index;   /* resident key -> slot */
    uint64_t *keys; /* slot -> key */
    uint8_t *in_main;

    pb_ring admission; /* A1in: slots in arrival order */
    pb_ilist main;     /* Am: head is most recently used */
    uint32_t main_length;

    /* A1out: keys with no values behind them, newest at the list head. */
    uint64_t *ghost_keys;   /* ghost slot -> key */
    pb_ilist ghost_list;    /* order; the tail is the oldest ghost */
    pb_map ghost_index;     /* ghost key -> its slot */
    uint32_t *ghost_free;   /* unused ghost slots */
    uint32_t ghost_free_count;

    uint32_t *free_slots;
    uint32_t free_count;
} pb_cache_2q_state;

static void two_q_release(pb_cache_2q_state *self)
{
    const pb_allocator *allocator = self->allocator;
    size_t slots = (size_t)self->slot_count;

    pb_map_destroy(&self->index, allocator);
    pb_map_destroy(&self->ghost_index, allocator);
    pb_ring_destroy(&self->admission, allocator);
    pb_ilist_destroy(&self->main, allocator);
    pb_ilist_destroy(&self->ghost_list, allocator);
    pb_free(allocator, self->keys, slots * sizeof(uint64_t));
    pb_free(allocator, self->in_main, slots * sizeof(uint8_t));
    pb_free(allocator, self->free_slots, slots * sizeof(uint32_t));
    pb_free(allocator, self->ghost_keys, (size_t)self->kout_size * sizeof(uint64_t));
    pb_free(allocator, self->ghost_free, (size_t)self->kout_size * sizeof(uint32_t));
    pb_free(allocator, self, sizeof(pb_cache_2q_state));
}

static pb_cache *two_q_create(const void *params, const pb_allocator *allocator, pb_rng *rng)
{
    const pb_cache_2q_params *config = (const pb_cache_2q_params *)params;
    pb_cache_2q_state *self;
    uint32_t capacity;
    uint32_t slot_count;
    uint32_t slot;
    double kin;
    double kout;

    (void)rng; /* 2Q makes no random choices */

    capacity = (config == NULL) ? 1000u : config->capacity;
    kin = (config == NULL) ? 0.25 : config->kin;
    kout = (config == NULL) ? 0.5 : config->kout;

    if (capacity == 0u || capacity == UINT32_MAX) {
        return NULL;
    }
    if (!(kin > 0.0) || kin > 1.0 || !(kout > 0.0) || kout > 1.0) {
        return NULL;
    }
    slot_count = capacity + 1u;

    self = (pb_cache_2q_state *)pb_alloc(allocator, sizeof(pb_cache_2q_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->capacity = capacity;
    self->slot_count = slot_count;
    /* Both fractions floor to at least one entry. */
    self->kin_size = (uint32_t)((double)capacity * kin);
    if (self->kin_size == 0u) {
        self->kin_size = 1u;
    }
    self->kout_size = (uint32_t)((double)capacity * kout);
    if (self->kout_size == 0u) {
        self->kout_size = 1u;
    }

    self->index.capacity = 0;
    self->ghost_index.capacity = 0;
    self->admission.capacity = 0;
    self->main.capacity = 0;
    self->ghost_list.capacity = 0;
    self->main_length = 0;
    self->ghost_free_count = 0;
    self->free_count = 0;

    self->keys = (uint64_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint64_t));
    self->in_main = (uint8_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint8_t));
    self->free_slots = (uint32_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint32_t));
    self->ghost_keys =
        (uint64_t *)pb_alloc(allocator, (size_t)self->kout_size * sizeof(uint64_t));
    self->ghost_free =
        (uint32_t *)pb_alloc(allocator, (size_t)self->kout_size * sizeof(uint32_t));

    if (self->keys == NULL || self->in_main == NULL || self->free_slots == NULL ||
        self->ghost_keys == NULL || self->ghost_free == NULL ||
        !pb_map_init(&self->index, slot_count, allocator) ||
        !pb_map_init(&self->ghost_index, self->kout_size, allocator) ||
        !pb_ring_init(&self->admission, slot_count, allocator) ||
        !pb_ilist_init(&self->main, slot_count, allocator) ||
        !pb_ilist_init(&self->ghost_list, self->kout_size, allocator)) {
        two_q_release(self);
        return NULL;
    }

    for (slot = 0; slot < slot_count; ++slot) {
        self->in_main[slot] = 0u;
        self->free_slots[slot] = slot_count - 1u - slot;
    }
    self->free_count = slot_count;
    for (slot = 0; slot < self->kout_size; ++slot) {
        self->ghost_free[slot] = self->kout_size - 1u - slot;
    }
    self->ghost_free_count = self->kout_size;

    return (pb_cache *)self;
}

static void two_q_destroy(pb_cache *cache)
{
    pb_cache_2q_state *self = (pb_cache_2q_state *)cache;

    if (self == NULL) {
        return;
    }
    two_q_release(self);
}

static void two_q_remember_ghost(pb_cache_2q_state *self, uint64_t key)
{
    uint32_t ghost_slot;

    if (self->ghost_list.length == self->kout_size) {
        /* A key whose ghost has expired has to start from A1in again. */
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

static void two_q_on_access(pb_cache *cache, uint64_t key, bool hit, const pb_cache_meta *meta)
{
    pb_cache_2q_state *self = (pb_cache_2q_state *)cache;
    uint32_t slot;
    uint32_t ghost_slot;

    (void)meta;
    assert(self != NULL);

    if (hit) {
        if (!pb_map_get(&self->index, key, &slot)) {
            assert(false); /* the caller's residency tracking is wrong */
            return;
        }
        /*
         * A hit in Am refreshes recency. A hit in A1in does nothing at all: the
         * key has not yet earned promotion, and reordering A1in would make it a
         * second LRU rather than an audition.
         */
        if (self->in_main[slot] != 0u) {
            pb_ilist_move_to_front(&self->main, slot);
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
    (void)pb_map_put(&self->index, key, slot);

    if (pb_map_get(&self->ghost_index, key, &ghost_slot)) {
        /* The second access the policy has been waiting for. Promotion removes
         * exactly this ghost; the others keep their order and their claim on
         * A1out's capacity. */
        (void)pb_map_remove(&self->ghost_index, key);
        pb_ilist_remove(&self->ghost_list, ghost_slot);
        self->ghost_free[self->ghost_free_count] = ghost_slot;
        self->ghost_free_count += 1u;
        self->in_main[slot] = 1u;
        pb_ilist_push_front(&self->main, slot);
        self->main_length += 1u;
        return;
    }

    self->in_main[slot] = 0u;
    (void)pb_ring_push_back(&self->admission, slot);
}

static uint64_t two_q_evict(pb_cache *cache)
{
    pb_cache_2q_state *self = (pb_cache_2q_state *)cache;
    uint32_t slot;
    uint64_t key;
    bool take_from_admission;

    assert(self != NULL);

    /* Drain A1in while it is over its share. */
    take_from_admission = self->admission.length > 0u &&
                          (self->admission.length > self->kin_size || self->main_length == 0u);

    if (take_from_admission) {
        slot = pb_ring_pop_front(&self->admission);
        key = self->keys[slot];
        two_q_remember_ghost(self, key);
    } else {
        if (self->main_length == 0u) {
            assert(false);
            return 0u;
        }
        slot = pb_ilist_pop_back(&self->main);
        if (slot == PB_ILIST_NIL) {
            return 0u;
        }
        self->main_length -= 1u;
        key = self->keys[slot];
        /* No ghost: an Am entry has already proven itself once. */
    }

    (void)pb_map_remove(&self->index, key);
    self->in_main[slot] = 0u;
    self->free_slots[self->free_count] = slot;
    self->free_count += 1u;
    return key;
}

static size_t two_q_memory_bytes(const pb_cache *cache)
{
    const pb_cache_2q_state *self = (const pb_cache_2q_state *)cache;

    if (self == NULL) {
        return 0;
    }
    return sizeof(pb_cache_2q_state) +
           (size_t)self->slot_count * (sizeof(uint64_t) + sizeof(uint8_t) + sizeof(uint32_t)) +
           (size_t)self->kout_size * (sizeof(uint64_t) + sizeof(uint32_t)) +
           pb_map_memory_bytes(&self->index) + pb_map_memory_bytes(&self->ghost_index) +
           pb_ring_memory_bytes(&self->admission) + pb_ilist_memory_bytes(&self->main) +
           pb_ilist_memory_bytes(&self->ghost_list);
}

const pb_cache_vtable pb_cache_2q = {
    two_q_create,
    two_q_on_access,
    two_q_evict,
    NULL, /* admits everything */
    two_q_destroy,
    two_q_memory_bytes,
    false, /* allocates_after_create */
    "cache/2q"
};
