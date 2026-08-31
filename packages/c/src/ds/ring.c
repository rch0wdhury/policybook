#include "policybook/ds/ring.h"

#include <assert.h>

bool pb_ring_init(pb_ring *ring, uint32_t capacity, const pb_allocator *allocator)
{
    assert(ring != NULL);

    ring->slots = NULL;
    ring->head = 0;
    ring->length = 0;
    ring->capacity = 0;

    if (capacity == 0u) {
        return false;
    }

    ring->slots = (uint32_t *)pb_alloc(allocator, (size_t)capacity * sizeof(uint32_t));
    if (ring->slots == NULL) {
        return false;
    }

    ring->capacity = capacity;
    return true;
}

void pb_ring_destroy(pb_ring *ring, const pb_allocator *allocator)
{
    if (ring == NULL || ring->capacity == 0u) {
        return;
    }

    pb_free(allocator, ring->slots, (size_t)ring->capacity * sizeof(uint32_t));
    ring->slots = NULL;
    ring->head = 0;
    ring->length = 0;
    ring->capacity = 0;
}

void pb_ring_clear(pb_ring *ring)
{
    assert(ring != NULL);
    ring->head = 0;
    ring->length = 0;
}

bool pb_ring_push_back(pb_ring *ring, uint32_t value)
{
    uint32_t tail;

    assert(ring != NULL);

    if (ring->length >= ring->capacity) {
        return false;
    }

    /* head + length may wrap; the modulo keeps it inside the array. */
    tail = ring->head + ring->length;
    if (tail >= ring->capacity) {
        tail -= ring->capacity;
    }

    ring->slots[tail] = value;
    ring->length += 1u;
    return true;
}

uint32_t pb_ring_pop_front(pb_ring *ring)
{
    uint32_t value;

    assert(ring != NULL);

    if (ring->length == 0u) {
        return PB_RING_NIL;
    }

    value = ring->slots[ring->head];
    ring->head += 1u;
    if (ring->head >= ring->capacity) {
        ring->head = 0;
    }
    ring->length -= 1u;
    return value;
}

uint32_t pb_ring_peek_front(const pb_ring *ring)
{
    assert(ring != NULL);

    if (ring->length == 0u) {
        return PB_RING_NIL;
    }
    return ring->slots[ring->head];
}

uint32_t pb_ring_at(const pb_ring *ring, uint32_t offset)
{
    uint32_t index;

    assert(ring != NULL);

    if (offset >= ring->length) {
        return PB_RING_NIL;
    }

    index = ring->head + offset;
    if (index >= ring->capacity) {
        index -= ring->capacity;
    }
    return ring->slots[index];
}

size_t pb_ring_memory_bytes(const pb_ring *ring)
{
    assert(ring != NULL);
    return (size_t)ring->capacity * sizeof(uint32_t);
}
