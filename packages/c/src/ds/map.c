#include "policybook/ds/map.h"

#include <assert.h>
#include <string.h>

/* Keep the table at most half full so linear probing stays short. */
#define PB_MAP_MIN_CAPACITY 8u

/*
 * splitmix64's finaliser, folded to 32 bits.
 *
 * Cache keys are frequently small integers or sequential ids, which land in
 * consecutive buckets under a weaker hash and turn linear probing into a linear
 * scan. This mixes them properly. The choice does not affect any policy's
 * decisions — only how quickly it finds them — because iteration order is never
 * exposed.
 */
static uint32_t pb_map_hash(uint64_t key)
{
    uint64_t z = key + 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z = z ^ (z >> 31);
    return (uint32_t)z;
}

bool pb_map_init(pb_map *map, uint32_t min_entries, const pb_allocator *allocator)
{
    uint64_t sized = PB_MAP_MIN_CAPACITY;
    uint32_t capacity;

    assert(map != NULL);

    map->keys = NULL;
    map->values = NULL;
    map->occupied = NULL;
    map->mask = 0;
    map->capacity = 0;
    map->count = 0;

    if (min_entries == 0u) {
        return false;
    }

    /* Smallest power of two with room for min_entries at 50% load. Sized in
     * 64 bits: `min_entries * 2` wraps a uint32 from 2^31 entries up, and no
     * uint32-indexed table can serve those anyway. */
    while (sized < (uint64_t)min_entries * 2u) {
        sized *= 2u;
    }
    if (sized > (UINT64_C(1) << 31)) {
        return false; /* the request cannot be sized, let alone allocated */
    }
    capacity = (uint32_t)sized;

    map->keys = (uint64_t *)pb_alloc(allocator, (size_t)capacity * sizeof(uint64_t));
    if (map->keys == NULL) {
        return false;
    }
    map->values = (uint32_t *)pb_alloc(allocator, (size_t)capacity * sizeof(uint32_t));
    if (map->values == NULL) {
        pb_free(allocator, map->keys, (size_t)capacity * sizeof(uint64_t));
        map->keys = NULL;
        return false;
    }
    map->occupied = (uint8_t *)pb_alloc(allocator, (size_t)capacity * sizeof(uint8_t));
    if (map->occupied == NULL) {
        pb_free(allocator, map->keys, (size_t)capacity * sizeof(uint64_t));
        pb_free(allocator, map->values, (size_t)capacity * sizeof(uint32_t));
        map->keys = NULL;
        map->values = NULL;
        return false;
    }

    map->capacity = capacity;
    map->mask = capacity - 1u;
    pb_map_clear(map);
    return true;
}

void pb_map_destroy(pb_map *map, const pb_allocator *allocator)
{
    if (map == NULL || map->capacity == 0u) {
        return;
    }

    pb_free(allocator, map->keys, (size_t)map->capacity * sizeof(uint64_t));
    pb_free(allocator, map->values, (size_t)map->capacity * sizeof(uint32_t));
    pb_free(allocator, map->occupied, (size_t)map->capacity * sizeof(uint8_t));

    map->keys = NULL;
    map->values = NULL;
    map->occupied = NULL;
    map->mask = 0;
    map->capacity = 0;
    map->count = 0;
}

void pb_map_clear(pb_map *map)
{
    assert(map != NULL);
    if (map->capacity > 0u) {
        memset(map->occupied, 0, (size_t)map->capacity * sizeof(uint8_t));
    }
    map->count = 0;
}

bool pb_map_get(const pb_map *map, uint64_t key, uint32_t *value)
{
    uint32_t slot;

    assert(map != NULL);
    if (map->capacity == 0u) {
        return false;
    }

    slot = pb_map_hash(key) & map->mask;
    while (map->occupied[slot] != 0u) {
        if (map->keys[slot] == key) {
            if (value != NULL) {
                *value = map->values[slot];
            }
            return true;
        }
        slot = (slot + 1u) & map->mask;
    }
    return false;
}

bool pb_map_put(pb_map *map, uint64_t key, uint32_t value)
{
    uint32_t slot;
    uint32_t probes;

    assert(map != NULL);
    if (map->capacity == 0u) {
        return false;
    }

    /* The probe is bounded by the table: in a full table no slot is ever
     * empty, and an unbounded walk would cycle forever. */
    slot = pb_map_hash(key) & map->mask;
    for (probes = 0u; probes < map->capacity; ++probes) {
        if (map->occupied[slot] == 0u) {
            map->occupied[slot] = 1u;
            map->keys[slot] = key;
            map->values[slot] = value;
            map->count += 1u;
            return true;
        }
        if (map->keys[slot] == key) {
            map->values[slot] = value;
            return true;
        }
        slot = (slot + 1u) & map->mask;
    }

    return false; /* every slot holds some other key */
}

bool pb_map_remove(pb_map *map, uint64_t key)
{
    uint32_t hole;
    uint32_t probe;

    assert(map != NULL);
    if (map->capacity == 0u) {
        return false;
    }

    hole = pb_map_hash(key) & map->mask;
    while (map->occupied[hole] != 0u) {
        if (map->keys[hole] == key) {
            break;
        }
        hole = (hole + 1u) & map->mask;
    }
    if (map->occupied[hole] == 0u) {
        return false;
    }

    map->occupied[hole] = 0u;
    map->count -= 1u;

    /*
     * Backward-shift deletion. Walk forward from the hole; any entry whose
     * ideal slot is at or before the hole (cyclically) can be moved back into
     * it, which keeps every probe chain contiguous and leaves no tombstone.
     */
    probe = hole;
    for (;;) {
        uint32_t ideal;

        probe = (probe + 1u) & map->mask;
        if (map->occupied[probe] == 0u) {
            break;
        }

        ideal = pb_map_hash(map->keys[probe]) & map->mask;

        /* Is `hole` inside the span [ideal, probe] once wrap-around is taken
         * into account? If so, moving the entry back keeps it findable. */
        if ((probe > hole) ? (ideal <= hole || ideal > probe)
                           : (ideal <= hole && ideal > probe)) {
            map->keys[hole] = map->keys[probe];
            map->values[hole] = map->values[probe];
            map->occupied[hole] = 1u;
            map->occupied[probe] = 0u;
            hole = probe;
        }
    }

    return true;
}

size_t pb_map_memory_bytes(const pb_map *map)
{
    assert(map != NULL);
    return (size_t)map->capacity *
           (sizeof(uint64_t) + sizeof(uint32_t) + sizeof(uint8_t));
}
