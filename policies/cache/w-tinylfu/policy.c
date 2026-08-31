/*
 * W-TinyLFU — frequency-based admission on four bits per counter.
 *
 * Mirrors index.ts and policy.py, including every sketch parameter: the three
 * implementations must produce identical estimates from identical accesses.
 */

#include "policybook/cache/w_tinylfu.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "policybook/allocator.h"
#include "policybook/ds/map.h"
#include "policybook/rng.h"

#define PB_WTLFU_NIL 0xFFFFFFFFu

/* Segments, used to index the head/tail/length arrays. */
#define PB_WTLFU_WINDOW 0u
#define PB_WTLFU_PROBATION 1u
#define PB_WTLFU_PROTECTED 2u
#define PB_WTLFU_SEGMENTS 3u

#define PB_WTLFU_ROWS 4u
/* Counters saturate here; four bits cannot hold more. */
#define PB_WTLFU_MAX_COUNT 15u

/* Row salts for the sketch. Arbitrary odd constants with good bit mixing. */
static const uint32_t PB_WTLFU_SALT[PB_WTLFU_ROWS] = { 0x9E3779B9u, 0x85EBCA6Bu, 0xC2B2AE35u,
                                                       0x27D4EB2Fu };

typedef struct pb_cache_w_tinylfu_state {
    const pb_allocator *allocator;
    uint32_t capacity;
    uint32_t slot_count;
    uint32_t window_size;
    uint32_t main_size;
    uint32_t protected_size;

    pb_map index;
    uint64_t *keys;

    uint32_t *next;
    uint32_t *prev;
    uint8_t *segment_of;
    uint32_t heads[PB_WTLFU_SEGMENTS];
    uint32_t tails[PB_WTLFU_SEGMENTS];
    uint32_t lengths[PB_WTLFU_SEGMENTS];

    uint32_t *free_slots;
    uint32_t free_count;

    uint8_t *sketch; /* four-bit counters, two to a byte */
    uint32_t sketch_width;
    uint32_t sketch_mask;
    uint32_t sketch_bytes;

    uint8_t *doorkeeper;
    uint32_t doorkeeper_mask;
    uint32_t doorkeeper_bytes;

    uint32_t sampled;
    uint32_t sample_limit;
} pb_cache_w_tinylfu_state;

static uint64_t pb_wtlfu_next_power_of_two(uint64_t value)
{
    uint64_t result = 1u;

    while (result < value) {
        result *= 2u;
    }
    return result;
}

static void pb_wtlfu_release(pb_cache_w_tinylfu_state *self)
{
    const pb_allocator *allocator = self->allocator;
    size_t slots = (size_t)self->slot_count;

    pb_map_destroy(&self->index, allocator);
    pb_free(allocator, self->keys, slots * sizeof(uint64_t));
    pb_free(allocator, self->next, slots * sizeof(uint32_t));
    pb_free(allocator, self->prev, slots * sizeof(uint32_t));
    pb_free(allocator, self->segment_of, slots * sizeof(uint8_t));
    pb_free(allocator, self->free_slots, slots * sizeof(uint32_t));
    pb_free(allocator, self->sketch, (size_t)self->sketch_bytes);
    pb_free(allocator, self->doorkeeper, (size_t)self->doorkeeper_bytes);
    pb_free(allocator, self, sizeof(pb_cache_w_tinylfu_state));
}

static pb_cache *wtlfu_create(const void *params, const pb_allocator *allocator, pb_rng *rng)
{
    const pb_cache_w_tinylfu_params *config = (const pb_cache_w_tinylfu_params *)params;
    pb_cache_w_tinylfu_state *self;
    uint32_t capacity;
    uint32_t slot_count;
    uint32_t slot;
    uint32_t segment;
    uint64_t sketch_width;
    double window_fraction;
    double protected_fraction;

    (void)rng; /* W-TinyLFU makes no random choices */

    capacity = (config == NULL) ? 1000u : config->capacity;
    window_fraction = (config == NULL) ? 0.01 : config->window_fraction;
    protected_fraction = (config == NULL) ? 0.8 : config->protected_fraction;

    /* Capacity 1 has no room for both a window and a main cache. The sketch
     * is eight positions per entry rounded up to a power of two, addressed as
     * row * width + slot in 32 bits: four rows of next_pow2(8 * capacity)
     * positions must stay within 2^32, which caps capacity at 2^27. */
    if (capacity < 2u || capacity > (UINT32_C(1) << 27)) {
        return NULL;
    }
    if (!(window_fraction > 0.0) || window_fraction >= 1.0) {
        return NULL;
    }
    if (!(protected_fraction > 0.0) || protected_fraction >= 1.0) {
        return NULL;
    }

    slot_count = capacity + 1u;

    self = (pb_cache_w_tinylfu_state *)pb_alloc(allocator, sizeof(pb_cache_w_tinylfu_state));
    if (self == NULL) {
        return NULL;
    }

    self->allocator = allocator;
    self->capacity = capacity;
    self->slot_count = slot_count;
    self->index.capacity = 0;
    self->free_count = 0;
    self->sampled = 0;

    /* The window holds at least one entry, and never the whole cache. */
    self->window_size = (uint32_t)((double)capacity * window_fraction);
    if (self->window_size == 0u) {
        self->window_size = 1u;
    }
    self->main_size = capacity - self->window_size;
    self->protected_size = (uint32_t)((double)self->main_size * protected_fraction);
    if (self->protected_size == 0u) {
        self->protected_size = 1u;
    }

    /* Eight sketch positions per cached entry, rounded up to a power of two so
     * the modulo is a mask. Sized in 64 bits: the four rows of four-bit
     * counters take 2 * width bytes, which reaches 2^31 at the capacity bound
     * and would wrap a uint32 on the way to it. */
    sketch_width = pb_wtlfu_next_power_of_two((uint64_t)capacity * 8u);
    self->sketch_width = (uint32_t)sketch_width;
    self->sketch_mask = (uint32_t)(sketch_width - 1u);
    self->sketch_bytes = (uint32_t)((PB_WTLFU_ROWS * sketch_width) / 2u);
    self->doorkeeper_mask = (uint32_t)(sketch_width - 1u);
    self->doorkeeper_bytes = (uint32_t)(sketch_width / 8u);

    /* Halve the counters every ten accesses per cached entry. */
    self->sample_limit = capacity * 10u;

    self->keys = (uint64_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint64_t));
    self->next = (uint32_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint32_t));
    self->prev = (uint32_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint32_t));
    self->segment_of = (uint8_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint8_t));
    self->free_slots = (uint32_t *)pb_alloc(allocator, (size_t)slot_count * sizeof(uint32_t));
    self->sketch = (uint8_t *)pb_alloc(allocator, (size_t)self->sketch_bytes);
    self->doorkeeper = (uint8_t *)pb_alloc(allocator, (size_t)self->doorkeeper_bytes);

    if (self->keys == NULL || self->next == NULL || self->prev == NULL ||
        self->segment_of == NULL || self->free_slots == NULL || self->sketch == NULL ||
        self->doorkeeper == NULL || !pb_map_init(&self->index, slot_count, allocator)) {
        pb_wtlfu_release(self);
        return NULL;
    }

    for (slot = 0; slot < slot_count; ++slot) {
        self->next[slot] = PB_WTLFU_NIL;
        self->prev[slot] = PB_WTLFU_NIL;
        self->segment_of[slot] = (uint8_t)PB_WTLFU_SEGMENTS;
        self->free_slots[slot] = slot_count - 1u - slot;
    }
    self->free_count = slot_count;

    for (segment = 0; segment < PB_WTLFU_SEGMENTS; ++segment) {
        self->heads[segment] = PB_WTLFU_NIL;
        self->tails[segment] = PB_WTLFU_NIL;
        self->lengths[segment] = 0;
    }

    memset(self->sketch, 0, (size_t)self->sketch_bytes);
    memset(self->doorkeeper, 0, (size_t)self->doorkeeper_bytes);

    return (pb_cache *)self;
}

static void wtlfu_destroy(pb_cache *cache)
{
    pb_cache_w_tinylfu_state *self = (pb_cache_w_tinylfu_state *)cache;

    if (self == NULL) {
        return;
    }
    pb_wtlfu_release(self);
}

/* --- the sketch ----------------------------------------------------------- */

static uint32_t pb_wtlfu_position(const pb_cache_w_tinylfu_state *self, uint32_t digest,
                                  uint32_t row)
{
    return pb_mix32(digest ^ PB_WTLFU_SALT[row]) & self->sketch_mask;
}

static uint32_t pb_wtlfu_counter_at(const pb_cache_w_tinylfu_state *self, uint32_t index)
{
    uint8_t byte = self->sketch[index >> 1];

    return ((index & 1u) == 0u) ? (uint32_t)(byte & 0x0Fu) : (uint32_t)(byte >> 4);
}

static void pb_wtlfu_increment(pb_cache_w_tinylfu_state *self, uint32_t index)
{
    uint32_t byte_index = index >> 1;
    uint8_t byte = self->sketch[byte_index];

    if ((index & 1u) == 0u) {
        uint8_t value = (uint8_t)(byte & 0x0Fu);
        if (value < PB_WTLFU_MAX_COUNT) {
            self->sketch[byte_index] = (uint8_t)((byte & 0xF0u) | (uint8_t)(value + 1u));
        }
    } else {
        uint8_t value = (uint8_t)(byte >> 4);
        if (value < PB_WTLFU_MAX_COUNT) {
            self->sketch[byte_index] =
                (uint8_t)((byte & 0x0Fu) | (uint8_t)((uint8_t)(value + 1u) << 4));
        }
    }
}

static uint32_t pb_wtlfu_doorkeeper_position(const pb_cache_w_tinylfu_state *self, uint32_t digest,
                                             uint32_t which)
{
    return pb_mix32(digest ^ PB_WTLFU_SALT[which]) & self->doorkeeper_mask;
}

static bool pb_wtlfu_doorkeeper_test(const pb_cache_w_tinylfu_state *self, uint32_t digest)
{
    uint32_t which;

    for (which = 0; which < 2u; ++which) {
        uint32_t bit = pb_wtlfu_doorkeeper_position(self, digest, which);
        if ((self->doorkeeper[bit >> 3] & (uint8_t)(1u << (bit & 7u))) == 0u) {
            return false;
        }
    }
    return true;
}

static void pb_wtlfu_doorkeeper_set(pb_cache_w_tinylfu_state *self, uint32_t digest)
{
    uint32_t which;

    for (which = 0; which < 2u; ++which) {
        uint32_t bit = pb_wtlfu_doorkeeper_position(self, digest, which);
        self->doorkeeper[bit >> 3] =
            (uint8_t)(self->doorkeeper[bit >> 3] | (uint8_t)(1u << (bit & 7u)));
    }
}

/*
 * Halve every counter and forget the doorkeeper.
 *
 * This is what stops W-TinyLFU becoming LFU: a key popular an hour ago decays
 * instead of holding its place forever. Shifting right and masking with 0x77
 * halves both counters in a byte without letting the high nibble's low bit leak
 * into the low one.
 */
static void pb_wtlfu_age(pb_cache_w_tinylfu_state *self)
{
    uint32_t index;

    for (index = 0; index < self->sketch_bytes; ++index) {
        self->sketch[index] = (uint8_t)((self->sketch[index] >> 1) & 0x77u);
    }
    memset(self->doorkeeper, 0, (size_t)self->doorkeeper_bytes);
    self->sampled = 0;
}

/*
 * Note an access.
 *
 * The doorkeeper absorbs a key's first appearance, so the very large number of
 * keys seen exactly once never consume sketch counters at all.
 */
static void pb_wtlfu_record(pb_cache_w_tinylfu_state *self, uint64_t key)
{
    uint32_t digest = pb_mix32((uint32_t)key);

    if (!pb_wtlfu_doorkeeper_test(self, digest)) {
        pb_wtlfu_doorkeeper_set(self, digest);
    } else {
        uint32_t row;
        for (row = 0; row < PB_WTLFU_ROWS; ++row) {
            pb_wtlfu_increment(self, row * self->sketch_width + pb_wtlfu_position(self, digest, row));
        }
    }

    self->sampled += 1u;
    if (self->sampled >= self->sample_limit) {
        pb_wtlfu_age(self);
    }
}

/* The count-min estimate, plus one if the doorkeeper has seen the key. */
static uint32_t pb_wtlfu_estimate(const pb_cache_w_tinylfu_state *self, uint64_t key)
{
    uint32_t digest = pb_mix32((uint32_t)key);
    uint32_t smallest = PB_WTLFU_MAX_COUNT;
    uint32_t row;

    for (row = 0; row < PB_WTLFU_ROWS; ++row) {
        uint32_t count =
            pb_wtlfu_counter_at(self, row * self->sketch_width + pb_wtlfu_position(self, digest, row));
        if (count < smallest) {
            smallest = count;
        }
    }

    return pb_wtlfu_doorkeeper_test(self, digest) ? smallest + 1u : smallest;
}

/* --- segments ------------------------------------------------------------- */

static void pb_wtlfu_link_front(pb_cache_w_tinylfu_state *self, uint32_t segment, uint32_t slot)
{
    uint32_t head = self->heads[segment];

    self->prev[slot] = PB_WTLFU_NIL;
    self->next[slot] = head;
    if (head != PB_WTLFU_NIL) {
        self->prev[head] = slot;
    } else {
        self->tails[segment] = slot;
    }
    self->heads[segment] = slot;
    self->segment_of[slot] = (uint8_t)segment;
    self->lengths[segment] += 1u;
}

static void pb_wtlfu_unlink(pb_cache_w_tinylfu_state *self, uint32_t slot)
{
    uint32_t segment = self->segment_of[slot];
    uint32_t following = self->next[slot];
    uint32_t preceding = self->prev[slot];

    assert(segment < PB_WTLFU_SEGMENTS);

    if (preceding != PB_WTLFU_NIL) {
        self->next[preceding] = following;
    } else {
        self->heads[segment] = following;
    }
    if (following != PB_WTLFU_NIL) {
        self->prev[following] = preceding;
    } else {
        self->tails[segment] = preceding;
    }

    self->next[slot] = PB_WTLFU_NIL;
    self->prev[slot] = PB_WTLFU_NIL;
    self->segment_of[slot] = (uint8_t)PB_WTLFU_SEGMENTS;
    self->lengths[segment] -= 1u;
}

static void pb_wtlfu_move_to_front(pb_cache_w_tinylfu_state *self, uint32_t segment, uint32_t slot)
{
    if (self->heads[segment] == slot) {
        return;
    }
    pb_wtlfu_unlink(self, slot);
    pb_wtlfu_link_front(self, segment, slot);
}

/* Move the window's overflow into the main cache while it has room. */
static void pb_wtlfu_drain_window(pb_cache_w_tinylfu_state *self)
{
    while (self->lengths[PB_WTLFU_WINDOW] > self->window_size &&
           self->lengths[PB_WTLFU_PROBATION] + self->lengths[PB_WTLFU_PROTECTED] <
               self->main_size) {
        uint32_t promoted = self->tails[PB_WTLFU_WINDOW];
        pb_wtlfu_unlink(self, promoted);
        pb_wtlfu_link_front(self, PB_WTLFU_PROBATION, promoted);
    }
}

/* The main cache's next victim: probation's oldest, or protected's. */
static uint32_t pb_wtlfu_main_victim(const pb_cache_w_tinylfu_state *self)
{
    if (self->tails[PB_WTLFU_PROBATION] != PB_WTLFU_NIL) {
        return self->tails[PB_WTLFU_PROBATION];
    }
    if (self->tails[PB_WTLFU_PROTECTED] != PB_WTLFU_NIL) {
        return self->tails[PB_WTLFU_PROTECTED];
    }
    return self->tails[PB_WTLFU_WINDOW];
}

static uint64_t pb_wtlfu_release_slot(pb_cache_w_tinylfu_state *self, uint32_t slot)
{
    uint64_t key = self->keys[slot];

    pb_wtlfu_unlink(self, slot);
    (void)pb_map_remove(&self->index, key);
    self->free_slots[self->free_count] = slot;
    self->free_count += 1u;
    return key;
}

static void wtlfu_on_access(pb_cache *cache, uint64_t key, bool hit, const pb_cache_meta *meta)
{
    pb_cache_w_tinylfu_state *self = (pb_cache_w_tinylfu_state *)cache;
    uint32_t slot;

    (void)meta;
    assert(self != NULL);

    /* Every access is evidence, whether or not the key is resident. Counting
     * misses is what lets a key earn admission before it is ever cached. */
    pb_wtlfu_record(self, key);

    if (hit) {
        uint32_t segment;

        if (!pb_map_get(&self->index, key, &slot)) {
            assert(false); /* the caller's residency tracking is wrong */
            return;
        }

        segment = self->segment_of[slot];
        if (segment == PB_WTLFU_WINDOW) {
            pb_wtlfu_move_to_front(self, PB_WTLFU_WINDOW, slot);
            return;
        }
        if (segment == PB_WTLFU_PROBATION) {
            /* A second hit promotes the entry out of reach of the contest. */
            pb_wtlfu_unlink(self, slot);
            pb_wtlfu_link_front(self, PB_WTLFU_PROTECTED, slot);
            if (self->lengths[PB_WTLFU_PROTECTED] > self->protected_size) {
                uint32_t demoted = self->tails[PB_WTLFU_PROTECTED];
                pb_wtlfu_unlink(self, demoted);
                pb_wtlfu_link_front(self, PB_WTLFU_PROBATION, demoted);
            }
            return;
        }
        pb_wtlfu_move_to_front(self, PB_WTLFU_PROTECTED, slot);
        return;
    }

    assert(self->free_count > 0u);
    if (self->free_count == 0u) {
        return;
    }

    /* New keys always enter the window; admission is contested later. */
    self->free_count -= 1u;
    slot = self->free_slots[self->free_count];
    self->keys[slot] = key;
    (void)pb_map_put(&self->index, key, slot);
    pb_wtlfu_link_front(self, PB_WTLFU_WINDOW, slot);

    /* The window keeps its size continuously, not only when the cache is full. */
    pb_wtlfu_drain_window(self);
}

static uint64_t wtlfu_evict(pb_cache *cache)
{
    pb_cache_w_tinylfu_state *self = (pb_cache_w_tinylfu_state *)cache;
    uint32_t victim;

    assert(self != NULL);

    /* Normally a no-op here: the window is drained on insertion. */
    pb_wtlfu_drain_window(self);

    if (self->lengths[PB_WTLFU_WINDOW] > self->window_size) {
        uint32_t candidate = self->tails[PB_WTLFU_WINDOW];

        victim = pb_wtlfu_main_victim(self);
        assert(victim != PB_WTLFU_NIL);
        if (victim == PB_WTLFU_NIL) {
            return 0u;
        }

        /*
         * Strictly greater: on a tie the incumbent stays. A resident entry has
         * demonstrated its frequency while the candidate has only an estimate,
         * and admitting on equal evidence would let a stream of one-hit wonders
         * churn the cache.
         */
        if (pb_wtlfu_estimate(self, self->keys[candidate]) >
            pb_wtlfu_estimate(self, self->keys[victim])) {
            pb_wtlfu_unlink(self, candidate);
            pb_wtlfu_link_front(self, PB_WTLFU_PROBATION, candidate);
            return pb_wtlfu_release_slot(self, victim);
        }
        return pb_wtlfu_release_slot(self, candidate);
    }

    victim = pb_wtlfu_main_victim(self);
    assert(victim != PB_WTLFU_NIL);
    if (victim == PB_WTLFU_NIL) {
        return 0u;
    }
    return pb_wtlfu_release_slot(self, victim);
}

static size_t wtlfu_memory_bytes(const pb_cache *cache)
{
    const pb_cache_w_tinylfu_state *self = (const pb_cache_w_tinylfu_state *)cache;

    if (self == NULL) {
        return 0;
    }
    return sizeof(pb_cache_w_tinylfu_state) +
           (size_t)self->slot_count *
               (sizeof(uint64_t) + 3u * sizeof(uint32_t) + sizeof(uint8_t)) +
           (size_t)self->sketch_bytes + (size_t)self->doorkeeper_bytes +
           pb_map_memory_bytes(&self->index);
}

const pb_cache_vtable pb_cache_w_tinylfu = {
    wtlfu_create,
    wtlfu_on_access,
    wtlfu_evict,
    NULL, /* admission is contested in evict, not at insertion */
    wtlfu_destroy,
    wtlfu_memory_bytes,
    false, /* allocates_after_create */
    "cache/w-tinylfu"
};
