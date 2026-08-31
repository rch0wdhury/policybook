/*
 * pb_ring — a fixed-capacity circular buffer of slot indices.
 *
 * The FIFO half of most cache policies: S3-FIFO's small and main queues, 2Q's
 * A1in, CLOCK's scan order, the sliding-window limiter's timestamps. Push and
 * pop are O(1) with no allocation and no memmove, and the buffer is contiguous,
 * which is what makes a scan over it cheap.
 */

#ifndef POLICYBOOK_DS_RING_H
#define POLICYBOOK_DS_RING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "policybook/allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Returned by pop/peek when the ring is empty. */
#define PB_RING_NIL 0xFFFFFFFFu

typedef struct pb_ring {
    uint32_t *slots;
    uint32_t head; /* index into slots of the oldest entry */
    uint32_t length;
    uint32_t capacity;
} pb_ring;

/* Allocate a ring holding up to `capacity` entries. The only allocation. */
bool pb_ring_init(pb_ring *ring, uint32_t capacity, const pb_allocator *allocator);

/* Release the ring's array. Safe on a zeroed or already-destroyed ring. */
void pb_ring_destroy(pb_ring *ring, const pb_allocator *allocator);

/* Drop every entry, keeping the allocation. */
void pb_ring_clear(pb_ring *ring);

/* Append at the back. Returns false if full. */
bool pb_ring_push_back(pb_ring *ring, uint32_t value);

/* Remove and return the oldest entry, or PB_RING_NIL if empty. */
uint32_t pb_ring_pop_front(pb_ring *ring);

/* Read the oldest entry without removing it, or PB_RING_NIL if empty. */
uint32_t pb_ring_peek_front(const pb_ring *ring);

/*
 * Read the entry `offset` places behind the front.
 *
 * offset 0 is the oldest. Returns PB_RING_NIL if offset is past the end.
 */
uint32_t pb_ring_at(const pb_ring *ring, uint32_t offset);

static inline bool pb_ring_is_empty(const pb_ring *ring)
{
    return ring->length == 0u;
}

static inline bool pb_ring_is_full(const pb_ring *ring)
{
    return ring->length == ring->capacity;
}

/* Bytes held by the ring, for a policy's memory_bytes. */
size_t pb_ring_memory_bytes(const pb_ring *ring);

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_DS_RING_H */
