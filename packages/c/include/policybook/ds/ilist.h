/*
 * pb_ilist — a doubly linked list over a fixed set of slot indices.
 *
 * Cache policies are mostly bookkeeping over a fixed number of entries, and the
 * obvious implementation — malloc'd nodes with pointers — is the wrong one
 * here. It scatters the working set across the heap, makes memory use
 * unpredictable, and allocates on the hot path. This list instead stores two
 * uint32_t arrays indexed by slot, allocated once.
 *
 * A "node" is a slot index in [0, capacity). The caller owns whatever the slot
 * means; the list only orders them.
 *
 *   pb_ilist lru;
 *   pb_ilist_init(&lru, 1024, NULL);
 *   pb_ilist_push_front(&lru, slot);      // most recently used
 *   uint32_t victim = pb_ilist_pop_back(&lru);
 *   pb_ilist_destroy(&lru, NULL);
 */

#ifndef POLICYBOOK_DS_ILIST_H
#define POLICYBOOK_DS_ILIST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "policybook/allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

/* End of the list. */
#define PB_ILIST_NIL 0xFFFFFFFFu
/* A slot that is not currently in the list. */
#define PB_ILIST_UNLINKED 0xFFFFFFFEu

/* The largest capacity a list can hold, given the two sentinels above. */
#define PB_ILIST_MAX_CAPACITY 0xFFFFFFFEu

typedef struct pb_ilist {
    uint32_t *next;
    uint32_t *prev;
    uint32_t head;
    uint32_t tail;
    uint32_t capacity;
    uint32_t length;
} pb_ilist;

/*
 * Allocate a list holding slots [0, capacity).
 *
 * Returns false if allocation fails or capacity is out of range. This is the
 * only function here that allocates.
 */
bool pb_ilist_init(pb_ilist *list, uint32_t capacity, const pb_allocator *allocator);

/* Release the list's arrays. Safe on a zeroed or already-destroyed list. */
void pb_ilist_destroy(pb_ilist *list, const pb_allocator *allocator);

/* Remove every node, keeping the allocation. */
void pb_ilist_clear(pb_ilist *list);

/* True if `node` is currently linked. */
bool pb_ilist_contains(const pb_ilist *list, uint32_t node);

/* Link `node` at the head. The node must not already be linked. */
void pb_ilist_push_front(pb_ilist *list, uint32_t node);

/* Link `node` at the tail. The node must not already be linked. */
void pb_ilist_push_back(pb_ilist *list, uint32_t node);

/* Unlink `node`. The node must currently be linked. */
void pb_ilist_remove(pb_ilist *list, uint32_t node);

/* Move a linked node to the head. The common operation for LRU on a hit. */
void pb_ilist_move_to_front(pb_ilist *list, uint32_t node);

/* Move a linked node to the tail. */
void pb_ilist_move_to_back(pb_ilist *list, uint32_t node);

/* Unlink and return the head, or PB_ILIST_NIL if the list is empty. */
uint32_t pb_ilist_pop_front(pb_ilist *list);

/* Unlink and return the tail, or PB_ILIST_NIL if the list is empty. */
uint32_t pb_ilist_pop_back(pb_ilist *list);

/* Bytes held by the list, for a policy's memory_bytes. */
size_t pb_ilist_memory_bytes(const pb_ilist *list);

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_DS_ILIST_H */
