/*
 * pb_heap — a fixed-capacity 4-ary min-heap of (key, item) pairs.
 *
 * Four children per node rather than two: the tree is shallower, so sift-down
 * does fewer comparisons of cache lines, which is the cost that actually
 * dominates. Used wherever a policy needs "smallest score wins" — Bélády's OPT
 * picking the entry whose next use is furthest away, LFU variants, and so on.
 *
 * Ordering is a total order on (key, item): equal keys are broken by the lower
 * item index. That is not an implementation detail — it is the registry's
 * tie-breaking rule, and it is what makes a heap-driven
 * policy produce the same eviction as the TypeScript and Python versions.
 */

#ifndef POLICYBOOK_DS_HEAP_H
#define POLICYBOOK_DS_HEAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "policybook/allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_heap {
    uint64_t *keys;
    uint32_t *items;
    uint32_t size;
    uint32_t capacity;
} pb_heap;

/* Allocate a heap holding up to `capacity` entries. The only allocation. */
bool pb_heap_init(pb_heap *heap, uint32_t capacity, const pb_allocator *allocator);

/* Release the heap's arrays. Safe on a zeroed or already-destroyed heap. */
void pb_heap_destroy(pb_heap *heap, const pb_allocator *allocator);

/* Drop every entry, keeping the allocation. */
void pb_heap_clear(pb_heap *heap);

/* Insert a pair. Returns false if the heap is full. */
bool pb_heap_push(pb_heap *heap, uint64_t key, uint32_t item);

/*
 * Remove the smallest (key, item) pair.
 *
 * `key` and `item` may each be NULL if that half is not wanted. Returns false
 * if the heap is empty.
 */
bool pb_heap_pop_min(pb_heap *heap, uint64_t *key, uint32_t *item);

/* Read the smallest pair without removing it. Returns false if empty. */
bool pb_heap_peek_min(const pb_heap *heap, uint64_t *key, uint32_t *item);

/* Bytes held by the heap, for a policy's memory_bytes. */
size_t pb_heap_memory_bytes(const pb_heap *heap);

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_DS_HEAP_H */
