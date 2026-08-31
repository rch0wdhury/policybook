#include "policybook/ds/ilist.h"

#include <assert.h>
#include <string.h>

bool pb_ilist_init(pb_ilist *list, uint32_t capacity, const pb_allocator *allocator)
{
    size_t bytes;

    assert(list != NULL);

    list->next = NULL;
    list->prev = NULL;
    list->head = PB_ILIST_NIL;
    list->tail = PB_ILIST_NIL;
    list->capacity = 0;
    list->length = 0;

    if (capacity == 0u || capacity > PB_ILIST_MAX_CAPACITY) {
        return false;
    }

    bytes = (size_t)capacity * sizeof(uint32_t);
    list->next = (uint32_t *)pb_alloc(allocator, bytes);
    if (list->next == NULL) {
        return false;
    }
    list->prev = (uint32_t *)pb_alloc(allocator, bytes);
    if (list->prev == NULL) {
        pb_free(allocator, list->next, bytes);
        list->next = NULL;
        return false;
    }

    list->capacity = capacity;
    pb_ilist_clear(list);
    return true;
}

void pb_ilist_destroy(pb_ilist *list, const pb_allocator *allocator)
{
    size_t bytes;

    if (list == NULL || list->capacity == 0u) {
        return;
    }

    bytes = (size_t)list->capacity * sizeof(uint32_t);
    pb_free(allocator, list->next, bytes);
    pb_free(allocator, list->prev, bytes);

    list->next = NULL;
    list->prev = NULL;
    list->head = PB_ILIST_NIL;
    list->tail = PB_ILIST_NIL;
    list->capacity = 0;
    list->length = 0;
}

void pb_ilist_clear(pb_ilist *list)
{
    uint32_t i;

    assert(list != NULL);

    for (i = 0; i < list->capacity; ++i) {
        list->next[i] = PB_ILIST_UNLINKED;
        list->prev[i] = PB_ILIST_UNLINKED;
    }
    list->head = PB_ILIST_NIL;
    list->tail = PB_ILIST_NIL;
    list->length = 0;
}

bool pb_ilist_contains(const pb_ilist *list, uint32_t node)
{
    assert(list != NULL);
    assert(node < list->capacity);
    return list->next[node] != PB_ILIST_UNLINKED;
}

void pb_ilist_push_front(pb_ilist *list, uint32_t node)
{
    assert(list != NULL);
    assert(node < list->capacity);
    assert(!pb_ilist_contains(list, node));

    list->prev[node] = PB_ILIST_NIL;
    list->next[node] = list->head;

    if (list->head != PB_ILIST_NIL) {
        list->prev[list->head] = node;
    } else {
        list->tail = node;
    }
    list->head = node;
    list->length += 1u;
}

void pb_ilist_push_back(pb_ilist *list, uint32_t node)
{
    assert(list != NULL);
    assert(node < list->capacity);
    assert(!pb_ilist_contains(list, node));

    list->next[node] = PB_ILIST_NIL;
    list->prev[node] = list->tail;

    if (list->tail != PB_ILIST_NIL) {
        list->next[list->tail] = node;
    } else {
        list->head = node;
    }
    list->tail = node;
    list->length += 1u;
}

void pb_ilist_remove(pb_ilist *list, uint32_t node)
{
    uint32_t next;
    uint32_t prev;

    assert(list != NULL);
    assert(node < list->capacity);
    assert(pb_ilist_contains(list, node));

    next = list->next[node];
    prev = list->prev[node];

    if (prev != PB_ILIST_NIL) {
        list->next[prev] = next;
    } else {
        list->head = next;
    }

    if (next != PB_ILIST_NIL) {
        list->prev[next] = prev;
    } else {
        list->tail = prev;
    }

    list->next[node] = PB_ILIST_UNLINKED;
    list->prev[node] = PB_ILIST_UNLINKED;
    list->length -= 1u;
}

void pb_ilist_move_to_front(pb_ilist *list, uint32_t node)
{
    assert(list != NULL);
    assert(node < list->capacity);
    assert(pb_ilist_contains(list, node));

    if (list->head == node) {
        return;
    }
    pb_ilist_remove(list, node);
    pb_ilist_push_front(list, node);
}

void pb_ilist_move_to_back(pb_ilist *list, uint32_t node)
{
    assert(list != NULL);
    assert(node < list->capacity);
    assert(pb_ilist_contains(list, node));

    if (list->tail == node) {
        return;
    }
    pb_ilist_remove(list, node);
    pb_ilist_push_back(list, node);
}

uint32_t pb_ilist_pop_front(pb_ilist *list)
{
    uint32_t node;

    assert(list != NULL);

    node = list->head;
    if (node == PB_ILIST_NIL) {
        return PB_ILIST_NIL;
    }
    pb_ilist_remove(list, node);
    return node;
}

uint32_t pb_ilist_pop_back(pb_ilist *list)
{
    uint32_t node;

    assert(list != NULL);

    node = list->tail;
    if (node == PB_ILIST_NIL) {
        return PB_ILIST_NIL;
    }
    pb_ilist_remove(list, node);
    return node;
}

size_t pb_ilist_memory_bytes(const pb_ilist *list)
{
    assert(list != NULL);
    return (size_t)list->capacity * sizeof(uint32_t) * 2u;
}
