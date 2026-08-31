/*
 * LFU — evict the key used least often.
 *
 * Mirrors index.ts and policy.py, including the O(1) frequency-class
 * construction. Everything lives in index arrays taken in create; nothing
 * allocates afterwards, and no operation scans.
 */

#include "policybook/cache/lfu.h"

#include <assert.h>
#include <stddef.h>

#include "policybook/allocator.h"
#include "policybook/ds/map.h"

#define PB_LFU_NIL 0xFFFFFFFFu

typedef struct pb_cache_lfu_state {
    const pb_allocator *allocator;
    uint32_t capacity;
    uint32_t slot_count; /* capacity + 1: a caller inserts before evicting */

    pb_map index; /* key -> entry slot */
    uint64_t *keys;

    /* Entries, linked within their frequency class. */
    uint32_t *entry_next;
    uint32_t *entry_prev;
    uint32_t *entry_class;
    uint32_t *free_entries;
    uint32_t free_entry_count;

    /* Frequency classes, linked in ascending order of frequency. */
    uint32_t *class_freq;
    uint32_t *class_head;
    uint32_t *class_tail;
    uint32_t *class_next;
    uint32_t *class_prev;
    uint32_t *free_classes;
    uint32_t free_class_count;
    uint32_t class_list_head; /* the lowest frequency: the eviction candidates */
} pb_cache_lfu_state;

/* Ten uint32 arrays of slot_count, plus the key array. */
#define PB_LFU_U32_ARRAYS 10u

static void lfu_release(pb_cache_lfu_state *self)
{
    const pb_allocator *allocator = self->allocator;
    size_t keys_bytes = (size_t)self->slot_count * sizeof(uint64_t);
    size_t u32_bytes = (size_t)self->slot_count * sizeof(uint32_t);

    pb_map_destroy(&self->index, allocator);
    pb_free(allocator, self->keys, keys_bytes);
    pb_free(allocator, self->entry_next, u32_bytes);
    pb_free(allocator, self->entry_prev, u32_bytes);
    pb_free(allocator, self->entry_class, u32_bytes);
    pb_free(allocator, self->free_entries, u32_bytes);
    pb_free(allocator, self->class_freq, u32_bytes);
    pb_free(allocator, self->class_head, u32_bytes);
    pb_free(allocator, self->class_tail, u32_bytes);
    pb_free(allocator, self->class_next, u32_bytes);
    pb_free(allocator, self->class_prev, u32_bytes);
    pb_free(allocator, self->free_classes, u32_bytes);
    pb_free(allocator, self, sizeof(pb_cache_lfu_state));
}

static pb_cache *lfu_create(const void *params, const pb_allocator *allocator, pb_rng *rng)
{
    const pb_cache_lfu_params *config = (const pb_cache_lfu_params *)params;
    pb_cache_lfu_state *self;
    uint32_t capacity;
    uint32_t slot_count;
    size_t u32_bytes;
    uint32_t slot;
    bool ok;

    (void)rng; /* LFU makes no random choices */

    capacity = (config == NULL) ? 1000u : config->capacity;
    if (capacity == 0u || capacity == UINT32_MAX) {
        return NULL;
    }
    slot_count = capacity + 1u;
    u32_bytes = (size_t)slot_count * sizeof(uint32_t);

    self = (pb_cache_lfu_state *)pb_alloc(allocator, sizeof(pb_cache_lfu_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->capacity = capacity;
    self->slot_count = slot_count;
    self->index.capacity = 0;
    self->keys = (uint64_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint64_t));
    self->entry_next = (uint32_t *)pb_alloc(allocator, u32_bytes);
    self->entry_prev = (uint32_t *)pb_alloc(allocator, u32_bytes);
    self->entry_class = (uint32_t *)pb_alloc(allocator, u32_bytes);
    self->free_entries = (uint32_t *)pb_alloc(allocator, u32_bytes);
    self->class_freq = (uint32_t *)pb_alloc(allocator, u32_bytes);
    self->class_head = (uint32_t *)pb_alloc(allocator, u32_bytes);
    self->class_tail = (uint32_t *)pb_alloc(allocator, u32_bytes);
    self->class_next = (uint32_t *)pb_alloc(allocator, u32_bytes);
    self->class_prev = (uint32_t *)pb_alloc(allocator, u32_bytes);
    self->free_classes = (uint32_t *)pb_alloc(allocator, u32_bytes);

    ok = self->keys != NULL && self->entry_next != NULL && self->entry_prev != NULL &&
         self->entry_class != NULL && self->free_entries != NULL && self->class_freq != NULL &&
         self->class_head != NULL && self->class_tail != NULL && self->class_next != NULL &&
         self->class_prev != NULL && self->free_classes != NULL &&
         pb_map_init(&self->index, slot_count, allocator);
    if (!ok) {
        lfu_release(self);
        return NULL;
    }

    for (slot = 0; slot < slot_count; ++slot) {
        self->entry_next[slot] = PB_LFU_NIL;
        self->entry_prev[slot] = PB_LFU_NIL;
        self->entry_class[slot] = PB_LFU_NIL;
        self->free_entries[slot] = slot_count - 1u - slot;
        self->class_head[slot] = PB_LFU_NIL;
        self->class_tail[slot] = PB_LFU_NIL;
        self->class_next[slot] = PB_LFU_NIL;
        self->class_prev[slot] = PB_LFU_NIL;
        self->class_freq[slot] = 0;
        self->free_classes[slot] = slot_count - 1u - slot;
    }
    self->free_entry_count = slot_count;
    self->free_class_count = slot_count;
    self->class_list_head = PB_LFU_NIL;

    return (pb_cache *)self;
}

static void lfu_destroy(pb_cache *cache)
{
    pb_cache_lfu_state *self = (pb_cache_lfu_state *)cache;

    if (self == NULL) {
        return;
    }
    lfu_release(self);
}

static uint32_t lfu_allocate_class(pb_cache_lfu_state *self, uint32_t frequency)
{
    uint32_t klass;

    assert(self->free_class_count > 0u);
    self->free_class_count -= 1u;
    klass = self->free_classes[self->free_class_count];
    self->class_freq[klass] = frequency;
    self->class_head[klass] = PB_LFU_NIL;
    self->class_tail[klass] = PB_LFU_NIL;
    return klass;
}

/* Create a class at the front of the ascending list. */
static uint32_t lfu_insert_class_front(pb_cache_lfu_state *self, uint32_t frequency)
{
    uint32_t klass = lfu_allocate_class(self, frequency);

    self->class_prev[klass] = PB_LFU_NIL;
    self->class_next[klass] = self->class_list_head;
    if (self->class_list_head != PB_LFU_NIL) {
        self->class_prev[self->class_list_head] = klass;
    }
    self->class_list_head = klass;
    return klass;
}

static uint32_t lfu_insert_class_after(pb_cache_lfu_state *self, uint32_t after,
                                       uint32_t frequency)
{
    uint32_t klass = lfu_allocate_class(self, frequency);
    uint32_t following = self->class_next[after];

    self->class_prev[klass] = after;
    self->class_next[klass] = following;
    self->class_next[after] = klass;
    if (following != PB_LFU_NIL) {
        self->class_prev[following] = klass;
    }
    return klass;
}

static void lfu_release_class(pb_cache_lfu_state *self, uint32_t klass)
{
    uint32_t following = self->class_next[klass];
    uint32_t preceding = self->class_prev[klass];

    if (preceding != PB_LFU_NIL) {
        self->class_next[preceding] = following;
    } else {
        self->class_list_head = following;
    }
    if (following != PB_LFU_NIL) {
        self->class_prev[following] = preceding;
    }

    self->class_next[klass] = PB_LFU_NIL;
    self->class_prev[klass] = PB_LFU_NIL;
    self->free_classes[self->free_class_count] = klass;
    self->free_class_count += 1u;
}

static void lfu_append_entry(pb_cache_lfu_state *self, uint32_t klass, uint32_t entry)
{
    uint32_t tail = self->class_tail[klass];

    self->entry_class[entry] = klass;
    self->entry_next[entry] = PB_LFU_NIL;
    self->entry_prev[entry] = tail;

    if (tail != PB_LFU_NIL) {
        self->entry_next[tail] = entry;
    } else {
        self->class_head[klass] = entry;
    }
    self->class_tail[klass] = entry;
}

static void lfu_remove_entry(pb_cache_lfu_state *self, uint32_t klass, uint32_t entry)
{
    uint32_t following = self->entry_next[entry];
    uint32_t preceding = self->entry_prev[entry];

    if (preceding != PB_LFU_NIL) {
        self->entry_next[preceding] = following;
    } else {
        self->class_head[klass] = following;
    }
    if (following != PB_LFU_NIL) {
        self->entry_prev[following] = preceding;
    } else {
        self->class_tail[klass] = preceding;
    }

    self->entry_next[entry] = PB_LFU_NIL;
    self->entry_prev[entry] = PB_LFU_NIL;
    self->entry_class[entry] = PB_LFU_NIL;

    /* An empty class carries no information and must not stay in the list. */
    if (self->class_head[klass] == PB_LFU_NIL) {
        lfu_release_class(self, klass);
    }
}

static void lfu_on_access(pb_cache *cache, uint64_t key, bool hit, const pb_cache_meta *meta)
{
    pb_cache_lfu_state *self = (pb_cache_lfu_state *)cache;
    uint32_t entry;
    uint32_t target;

    (void)meta;
    assert(self != NULL);

    if (hit) {
        uint32_t from;
        uint32_t frequency;
        uint32_t following;

        if (!pb_map_get(&self->index, key, &entry)) {
            assert(false); /* the caller's residency tracking is wrong */
            return;
        }

        from = self->entry_class[entry];
        frequency = self->class_freq[from];
        following = self->class_next[from];

        /* Reuse the neighbouring class if it is already the frequency we want. */
        if (following != PB_LFU_NIL && self->class_freq[following] == frequency + 1u) {
            target = following;
        } else {
            target = lfu_insert_class_after(self, from, frequency + 1u);
        }

        lfu_remove_entry(self, from, entry);
        lfu_append_entry(self, target, entry);
        return;
    }

    assert(self->free_entry_count > 0u);
    if (self->free_entry_count == 0u) {
        return;
    }

    self->free_entry_count -= 1u;
    entry = self->free_entries[self->free_entry_count];
    self->keys[entry] = key;
    (void)pb_map_put(&self->index, key, entry);

    /* A new entry has frequency 1, so it belongs at the front of the list. */
    if (self->class_list_head != PB_LFU_NIL && self->class_freq[self->class_list_head] == 1u) {
        target = self->class_list_head;
    } else {
        target = lfu_insert_class_front(self, 1u);
    }
    lfu_append_entry(self, target, entry);
}

static uint64_t lfu_evict(pb_cache *cache)
{
    pb_cache_lfu_state *self = (pb_cache_lfu_state *)cache;
    uint32_t klass;
    uint32_t entry;
    uint64_t key;

    assert(self != NULL);
    assert(self->class_list_head != PB_LFU_NIL);
    if (self->class_list_head == PB_LFU_NIL) {
        return 0u;
    }

    /* The first class holds the lowest frequency; its head reached that
     * frequency earliest, which is the documented tie-break. */
    klass = self->class_list_head;
    entry = self->class_head[klass];
    key = self->keys[entry];

    lfu_remove_entry(self, klass, entry);
    (void)pb_map_remove(&self->index, key);
    self->free_entries[self->free_entry_count] = entry;
    self->free_entry_count += 1u;

    return key;
}

static size_t lfu_memory_bytes(const pb_cache *cache)
{
    const pb_cache_lfu_state *self = (const pb_cache_lfu_state *)cache;

    if (self == NULL) {
        return 0;
    }
    return sizeof(pb_cache_lfu_state) + (size_t)self->slot_count * sizeof(uint64_t) +
           (size_t)self->slot_count * PB_LFU_U32_ARRAYS * sizeof(uint32_t) +
           pb_map_memory_bytes(&self->index);
}

const pb_cache_vtable pb_cache_lfu = {
    lfu_create,
    lfu_on_access,
    lfu_evict,
    NULL, /* admits everything */
    lfu_destroy,
    lfu_memory_bytes,
    false, /* allocates_after_create */
    "cache/lfu"
};
