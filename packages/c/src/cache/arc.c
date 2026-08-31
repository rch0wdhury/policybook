/*
 * GENERATED COPY — do not edit. Edit policies/cache/arc/policy.c instead,
 * then run: pnpm tsx scripts/assemble-c.ts
 */
/*
 * ARC — balance recency against frequency, and tune the balance itself.
 *
 * Mirrors index.ts and policy.py, implemented from the paper's Figure 4. One
 * slot pool serves all four lists, since a key is in exactly one of them; the
 * pool is sized 2c + 2 because every cached entry may also have a ghost.
 */

#include "policybook/cache/arc.h"

#include <assert.h>
#include <stddef.h>

#include "policybook/allocator.h"
#include "policybook/ds/map.h"

#define PB_ARC_NIL 0xFFFFFFFFu

/* The four lists, used to index the head/tail/length arrays. */
#define PB_ARC_T1 0u
#define PB_ARC_T2 1u
#define PB_ARC_B1 2u
#define PB_ARC_B2 3u
#define PB_ARC_LISTS 4u

typedef struct pb_cache_arc_state {
    const pb_allocator *allocator;
    uint32_t capacity;
    uint32_t slot_count;

    pb_map index; /* key -> slot, across all four lists */
    uint64_t *keys;

    uint32_t *next;
    uint32_t *prev;
    uint8_t *list_of;
    uint32_t heads[PB_ARC_LISTS];
    uint32_t tails[PB_ARC_LISTS];
    uint32_t lengths[PB_ARC_LISTS];

    uint32_t *free_slots;
    uint32_t free_count;

    uint32_t target; /* p: the target size for T1 */

    bool has_victim;
    uint64_t victim;
} pb_cache_arc_state;

static void arc_release(pb_cache_arc_state *self)
{
    const pb_allocator *allocator = self->allocator;
    size_t slots = (size_t)self->slot_count;

    pb_map_destroy(&self->index, allocator);
    pb_free(allocator, self->keys, slots * sizeof(uint64_t));
    pb_free(allocator, self->next, slots * sizeof(uint32_t));
    pb_free(allocator, self->prev, slots * sizeof(uint32_t));
    pb_free(allocator, self->list_of, slots * sizeof(uint8_t));
    pb_free(allocator, self->free_slots, slots * sizeof(uint32_t));
    pb_free(allocator, self, sizeof(pb_cache_arc_state));
}

static pb_cache *arc_create(const void *params, const pb_allocator *allocator, pb_rng *rng)
{
    const pb_cache_arc_params *config = (const pb_cache_arc_params *)params;
    pb_cache_arc_state *self;
    uint32_t capacity;
    uint32_t slot_count;
    uint32_t slot;
    uint32_t list;

    (void)rng; /* ARC makes no random choices */

    capacity = (config == NULL) ? 1000u : config->capacity;
    if (capacity == 0u || capacity > (UINT32_MAX - 2u) / 2u) {
        return NULL;
    }
    /* The cache holds at most c entries and the ghosts at most c more, plus a
     * spare for the entry in flight during a miss. */
    slot_count = 2u * capacity + 2u;

    self = (pb_cache_arc_state *)pb_alloc(allocator, sizeof(pb_cache_arc_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->capacity = capacity;
    self->slot_count = slot_count;
    self->index.capacity = 0;
    self->free_count = 0;
    self->target = 0;
    self->has_victim = false;
    self->victim = 0;

    self->keys = (uint64_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint64_t));
    self->next = (uint32_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint32_t));
    self->prev = (uint32_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint32_t));
    self->list_of = (uint8_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint8_t));
    self->free_slots = (uint32_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint32_t));

    if (self->keys == NULL || self->next == NULL || self->prev == NULL ||
        self->list_of == NULL || self->free_slots == NULL ||
        !pb_map_init(&self->index, slot_count, allocator)) {
        arc_release(self);
        return NULL;
    }

    for (slot = 0; slot < slot_count; ++slot) {
        self->next[slot] = PB_ARC_NIL;
        self->prev[slot] = PB_ARC_NIL;
        self->list_of[slot] = (uint8_t)PB_ARC_LISTS; /* in no list */
        self->free_slots[slot] = slot_count - 1u - slot;
    }
    self->free_count = slot_count;

    for (list = 0; list < PB_ARC_LISTS; ++list) {
        self->heads[list] = PB_ARC_NIL;
        self->tails[list] = PB_ARC_NIL;
        self->lengths[list] = 0;
    }

    return (pb_cache *)self;
}

static void arc_destroy(pb_cache *cache)
{
    pb_cache_arc_state *self = (pb_cache_arc_state *)cache;

    if (self == NULL) {
        return;
    }
    arc_release(self);
}

static void arc_link_front(pb_cache_arc_state *self, uint32_t list, uint32_t slot)
{
    uint32_t head = self->heads[list];

    self->prev[slot] = PB_ARC_NIL;
    self->next[slot] = head;
    if (head != PB_ARC_NIL) {
        self->prev[head] = slot;
    } else {
        self->tails[list] = slot;
    }
    self->heads[list] = slot;
    self->list_of[slot] = (uint8_t)list;
    self->lengths[list] += 1u;
}

static void arc_unlink(pb_cache_arc_state *self, uint32_t slot)
{
    uint32_t list = self->list_of[slot];
    uint32_t following = self->next[slot];
    uint32_t preceding = self->prev[slot];

    assert(list < PB_ARC_LISTS);

    if (preceding != PB_ARC_NIL) {
        self->next[preceding] = following;
    } else {
        self->heads[list] = following;
    }
    if (following != PB_ARC_NIL) {
        self->prev[following] = preceding;
    } else {
        self->tails[list] = preceding;
    }

    self->next[slot] = PB_ARC_NIL;
    self->prev[slot] = PB_ARC_NIL;
    self->list_of[slot] = (uint8_t)PB_ARC_LISTS;
    self->lengths[list] -= 1u;
}

static uint32_t arc_take_slot(pb_cache_arc_state *self, uint64_t key)
{
    uint32_t slot;

    assert(self->free_count > 0u);
    self->free_count -= 1u;
    slot = self->free_slots[self->free_count];
    self->keys[slot] = key;
    (void)pb_map_put(&self->index, key, slot);
    return slot;
}

static void arc_release_slot(pb_cache_arc_state *self, uint32_t slot)
{
    (void)pb_map_remove(&self->index, self->keys[slot]);
    self->free_slots[self->free_count] = slot;
    self->free_count += 1u;
}

/* Remove the oldest entry of a cache list, optionally leaving a ghost. */
static void arc_evict_oldest(pb_cache_arc_state *self, uint32_t list, uint32_t ghost)
{
    uint32_t slot = self->tails[list];

    assert(slot != PB_ARC_NIL);
    if (slot == PB_ARC_NIL) {
        return;
    }

    self->victim = self->keys[slot];
    self->has_victim = true;

    arc_unlink(self, slot);
    if (ghost >= PB_ARC_LISTS) {
        arc_release_slot(self, slot);
    } else {
        arc_link_front(self, ghost, slot);
    }
}

static void arc_drop_oldest_ghost(pb_cache_arc_state *self, uint32_t list)
{
    uint32_t slot = self->tails[list];

    if (slot == PB_ARC_NIL) {
        return;
    }
    arc_unlink(self, slot);
    arc_release_slot(self, slot);
}

/*
 * The paper's REPLACE: choose a victim from T1 or T2 and demote it to the
 * matching ghost list.
 *
 * T1 gives up an entry when it is over its target — or when it is exactly at
 * target and the key that caused this came back from B2, which is a hint that
 * the frequent side deserves the benefit of the doubt.
 */
static void arc_replace(pb_cache_arc_state *self, bool returning_from_b2)
{
    uint32_t t1 = self->lengths[PB_ARC_T1];
    bool take_from_t1 =
        t1 >= 1u && ((returning_from_b2 && t1 == self->target) || t1 > self->target);

    if (take_from_t1) {
        arc_evict_oldest(self, PB_ARC_T1, PB_ARC_B1);
    } else {
        arc_evict_oldest(self, PB_ARC_T2, PB_ARC_B2);
    }
}

static void arc_on_access(pb_cache *cache, uint64_t key, bool hit, const pb_cache_meta *meta)
{
    pb_cache_arc_state *self = (pb_cache_arc_state *)cache;
    uint32_t slot;
    uint32_t list;
    uint32_t b1;
    uint32_t b2;
    uint32_t t1;
    uint32_t total;
    bool known;

    (void)meta;
    assert(self != NULL);

    known = pb_map_get(&self->index, key, &slot);

    if (hit) {
        if (!known || self->list_of[slot] > (uint8_t)PB_ARC_T2) {
            assert(false); /* the caller's residency tracking is wrong */
            return;
        }
        /* Case I: a second recent use promotes the key to the frequent list. */
        arc_unlink(self, slot);
        arc_link_front(self, PB_ARC_T2, slot);
        return;
    }

    b1 = self->lengths[PB_ARC_B1];
    b2 = self->lengths[PB_ARC_B2];

    if (known) {
        list = self->list_of[slot];
        assert(list == PB_ARC_B1 || list == PB_ARC_B2);

        if (list == PB_ARC_B1) {
            /* Case II: recency was undervalued, so give T1 more room. */
            uint32_t delta = (b1 >= b2) ? 1u : (b2 / b1);
            self->target += delta;
            if (self->target > self->capacity) {
                self->target = self->capacity;
            }
            arc_replace(self, false);
        } else {
            /* Case III: frequency was undervalued, so take room away from T1. */
            uint32_t delta = (b2 >= b1) ? 1u : (b1 / b2);
            self->target = (self->target > delta) ? (self->target - delta) : 0u;
            arc_replace(self, true);
        }

        arc_unlink(self, slot);
        arc_link_front(self, PB_ARC_T2, slot);
        return;
    }

    /* Case IV: a key ARC has no memory of at all. */
    t1 = self->lengths[PB_ARC_T1];
    total = t1 + self->lengths[PB_ARC_T2] + b1 + b2;

    if (t1 + b1 == self->capacity) {
        if (t1 < self->capacity) {
            arc_drop_oldest_ghost(self, PB_ARC_B1);
            arc_replace(self, false);
        } else {
            /* No room for a ghost: |T1| + |B1| <= c is the invariant. */
            arc_evict_oldest(self, PB_ARC_T1, PB_ARC_LISTS);
        }
    } else if (t1 + b1 < self->capacity && total >= self->capacity) {
        if (total == 2u * self->capacity) {
            arc_drop_oldest_ghost(self, PB_ARC_B2);
        }
        arc_replace(self, false);
    }

    slot = arc_take_slot(self, key);
    arc_link_front(self, PB_ARC_T1, slot);
}

static uint64_t arc_evict(pb_cache *cache)
{
    pb_cache_arc_state *self = (pb_cache_arc_state *)cache;

    assert(self != NULL);
    assert(self->has_victim);
    if (!self->has_victim) {
        return 0u;
    }
    self->has_victim = false;
    return self->victim;
}

static size_t arc_memory_bytes(const pb_cache *cache)
{
    const pb_cache_arc_state *self = (const pb_cache_arc_state *)cache;

    if (self == NULL) {
        return 0;
    }
    return sizeof(pb_cache_arc_state) +
           (size_t)self->slot_count *
               (sizeof(uint64_t) + 3u * sizeof(uint32_t) + sizeof(uint8_t)) +
           pb_map_memory_bytes(&self->index);
}

const pb_cache_vtable pb_cache_arc = {
    arc_create,
    arc_on_access,
    arc_evict,
    NULL, /* admits everything */
    arc_destroy,
    arc_memory_bytes,
    false, /* allocates_after_create */
    "cache/arc"
};
