#include "policybook/ds/heap.h"

#include <assert.h>

/* Children of i are 4i+1 .. 4i+4; the parent of i is (i-1)/4. */
#define PB_HEAP_ARITY 4u

/* True if (key_a, item_a) sorts before (key_b, item_b). */
static bool pb_heap_less(uint64_t key_a, uint32_t item_a, uint64_t key_b, uint32_t item_b)
{
    if (key_a != key_b) {
        return key_a < key_b;
    }
    /* Ties break by the lower item index — the registry's rule. */
    return item_a < item_b;
}

static void pb_heap_sift_up(pb_heap *heap, uint32_t index)
{
    uint64_t key = heap->keys[index];
    uint32_t item = heap->items[index];

    while (index > 0u) {
        uint32_t parent = (index - 1u) / PB_HEAP_ARITY;
        if (!pb_heap_less(key, item, heap->keys[parent], heap->items[parent])) {
            break;
        }
        heap->keys[index] = heap->keys[parent];
        heap->items[index] = heap->items[parent];
        index = parent;
    }

    heap->keys[index] = key;
    heap->items[index] = item;
}

static void pb_heap_sift_down(pb_heap *heap, uint32_t index)
{
    uint64_t key = heap->keys[index];
    uint32_t item = heap->items[index];

    for (;;) {
        uint32_t first = index * PB_HEAP_ARITY + 1u;
        uint32_t best;
        uint32_t child;
        uint32_t last;

        if (first >= heap->size) {
            break;
        }

        last = first + PB_HEAP_ARITY;
        if (last > heap->size) {
            last = heap->size;
        }

        best = first;
        for (child = first + 1u; child < last; ++child) {
            if (pb_heap_less(heap->keys[child], heap->items[child], heap->keys[best],
                             heap->items[best])) {
                best = child;
            }
        }

        if (!pb_heap_less(heap->keys[best], heap->items[best], key, item)) {
            break;
        }

        heap->keys[index] = heap->keys[best];
        heap->items[index] = heap->items[best];
        index = best;
    }

    heap->keys[index] = key;
    heap->items[index] = item;
}

bool pb_heap_init(pb_heap *heap, uint32_t capacity, const pb_allocator *allocator)
{
    assert(heap != NULL);

    heap->keys = NULL;
    heap->items = NULL;
    heap->size = 0;
    heap->capacity = 0;

    if (capacity == 0u) {
        return false;
    }

    heap->keys = (uint64_t *)pb_alloc(allocator, (size_t)capacity * sizeof(uint64_t));
    if (heap->keys == NULL) {
        return false;
    }
    heap->items = (uint32_t *)pb_alloc(allocator, (size_t)capacity * sizeof(uint32_t));
    if (heap->items == NULL) {
        pb_free(allocator, heap->keys, (size_t)capacity * sizeof(uint64_t));
        heap->keys = NULL;
        return false;
    }

    heap->capacity = capacity;
    return true;
}

void pb_heap_destroy(pb_heap *heap, const pb_allocator *allocator)
{
    if (heap == NULL || heap->capacity == 0u) {
        return;
    }

    pb_free(allocator, heap->keys, (size_t)heap->capacity * sizeof(uint64_t));
    pb_free(allocator, heap->items, (size_t)heap->capacity * sizeof(uint32_t));

    heap->keys = NULL;
    heap->items = NULL;
    heap->size = 0;
    heap->capacity = 0;
}

void pb_heap_clear(pb_heap *heap)
{
    assert(heap != NULL);
    heap->size = 0;
}

bool pb_heap_push(pb_heap *heap, uint64_t key, uint32_t item)
{
    assert(heap != NULL);

    if (heap->size >= heap->capacity) {
        return false;
    }

    heap->keys[heap->size] = key;
    heap->items[heap->size] = item;
    heap->size += 1u;
    pb_heap_sift_up(heap, heap->size - 1u);
    return true;
}

bool pb_heap_peek_min(const pb_heap *heap, uint64_t *key, uint32_t *item)
{
    assert(heap != NULL);

    if (heap->size == 0u) {
        return false;
    }
    if (key != NULL) {
        *key = heap->keys[0];
    }
    if (item != NULL) {
        *item = heap->items[0];
    }
    return true;
}

bool pb_heap_pop_min(pb_heap *heap, uint64_t *key, uint32_t *item)
{
    assert(heap != NULL);

    if (heap->size == 0u) {
        return false;
    }
    if (key != NULL) {
        *key = heap->keys[0];
    }
    if (item != NULL) {
        *item = heap->items[0];
    }

    heap->size -= 1u;
    if (heap->size > 0u) {
        heap->keys[0] = heap->keys[heap->size];
        heap->items[0] = heap->items[heap->size];
        pb_heap_sift_down(heap, 0u);
    }
    return true;
}

size_t pb_heap_memory_bytes(const pb_heap *heap)
{
    assert(heap != NULL);
    return (size_t)heap->capacity * (sizeof(uint64_t) + sizeof(uint32_t));
}
