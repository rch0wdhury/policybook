/*
 * pb_map — a fixed-capacity hash map from uint64 key to uint32 slot.
 *
 * Cache policies need to answer "where is this key?" on every access, and in C
 * that means bringing your own hash map. This one is built for the same
 * constraints as everything else here: it takes all its memory in init, never
 * grows, never rehashes, and never allocates again.
 *
 * Open addressing with linear probing, and **backward-shift deletion** rather
 * than tombstones. Tombstones would degrade a long-running cache — every
 * eviction leaves one behind, probe chains lengthen without bound, and the
 * usual remedy is a rehash, which allocates. Shifting the following entries
 * back into the hole keeps the table exactly as clean as a fresh one, which is
 * what a cache that evicts millions of times needs.
 *
 * Iteration order is never exposed, deliberately: no policy decision may depend
 * on it.
 */

#ifndef POLICYBOOK_DS_MAP_H
#define POLICYBOOK_DS_MAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "policybook/allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_map {
    uint64_t *keys;
    uint32_t *values;
    uint8_t *occupied;
    uint32_t mask;     /* capacity - 1; capacity is a power of two */
    uint32_t capacity;
    uint32_t count;
} pb_map;

/*
 * Allocate a table that can hold at least `min_entries` without exceeding a
 * 50% load factor, so probe chains stay short.
 *
 * Returns false on a failed allocation, a zero capacity, or a `min_entries`
 * above 2^30, which no uint32-indexed table can hold at that load.
 */
bool pb_map_init(pb_map *map, uint32_t min_entries, const pb_allocator *allocator);

/* Release the table. Safe on a zeroed or already-destroyed map. */
void pb_map_destroy(pb_map *map, const pb_allocator *allocator);

/* Forget every entry, keeping the allocation. */
void pb_map_clear(pb_map *map);

/*
 * Look a key up.
 *
 * Writes the value through `value` (which may be NULL) and returns true if the
 * key is present.
 */
bool pb_map_get(const pb_map *map, uint64_t key, uint32_t *value);

/*
 * Insert or update.
 *
 * Returns false only if the table is full, which cannot happen when it was
 * sized for the number of entries the caller actually holds.
 */
bool pb_map_put(pb_map *map, uint64_t key, uint32_t value);

/* Remove a key. Returns true if it was present. */
bool pb_map_remove(pb_map *map, uint64_t key);

/* Bytes held, for a policy's memory_bytes. */
size_t pb_map_memory_bytes(const pb_map *map);

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_DS_MAP_H */
