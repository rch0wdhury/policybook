/*
 * Unit tests for the three shared data structures.
 *
 * These are the structures every C policy is built from, so a bug here would
 * surface as a mysterious eviction difference in some policy three milestones
 * from now. They are tested directly, including the ordering rules the registry
 * depends on.
 */

#include <stdint.h>
#include <string.h>

#include "policybook/allocator.h"
#include "policybook/ds/heap.h"
#include "policybook/ds/ilist.h"
#include "policybook/ds/map.h"
#include "policybook/ds/ring.h"
#include "policybook/hash.h"
#include "policybook/rng.h"

#include "pb_test.h"

/* --- a counting allocator, to prove nothing allocates after init ----------- */

typedef struct counting_allocator {
    int allocations;
    int frees;
    size_t live_bytes;
} counting_allocator;

static void *counting_alloc(void *ctx, size_t n)
{
    counting_allocator *state = (counting_allocator *)ctx;
    state->allocations += 1;
    state->live_bytes += n;
    return pb_allocator_default()->alloc(NULL, n);
}

static void counting_free(void *ctx, void *p, size_t n)
{
    counting_allocator *state = (counting_allocator *)ctx;
    state->frees += 1;
    state->live_bytes -= n;
    pb_allocator_default()->free(NULL, p, n);
}

/* --- pb_ilist -------------------------------------------------------------- */

static void test_ilist_basics(void)
{
    pb_ilist list;
    PB_CHECK(pb_ilist_init(&list, 8, NULL));
    PB_CHECK_U32(list.length, 0);
    PB_CHECK_U32(list.head, PB_ILIST_NIL);
    PB_CHECK_U32(list.tail, PB_ILIST_NIL);

    /* Nothing is linked to start with. */
    PB_CHECK(!pb_ilist_contains(&list, 0));
    PB_CHECK(!pb_ilist_contains(&list, 7));

    pb_ilist_push_front(&list, 3);
    PB_CHECK(pb_ilist_contains(&list, 3));
    PB_CHECK_U32(list.length, 1);
    /* A sole element is both head and tail, and is still "linked". */
    PB_CHECK_U32(list.head, 3);
    PB_CHECK_U32(list.tail, 3);

    pb_ilist_push_front(&list, 1);
    pb_ilist_push_back(&list, 5);
    /* Order is now 1, 3, 5. */
    PB_CHECK_U32(list.head, 1);
    PB_CHECK_U32(list.tail, 5);
    PB_CHECK_U32(list.next[1], 3);
    PB_CHECK_U32(list.next[3], 5);
    PB_CHECK_U32(list.next[5], PB_ILIST_NIL);
    PB_CHECK_U32(list.prev[1], PB_ILIST_NIL);
    PB_CHECK_U32(list.length, 3);

    /* Removing from the middle keeps both neighbours linked. */
    pb_ilist_remove(&list, 3);
    PB_CHECK(!pb_ilist_contains(&list, 3));
    PB_CHECK_U32(list.next[1], 5);
    PB_CHECK_U32(list.prev[5], 1);
    PB_CHECK_U32(list.length, 2);

    PB_CHECK_U32(pb_ilist_pop_front(&list), 1);
    PB_CHECK_U32(pb_ilist_pop_back(&list), 5);
    PB_CHECK_U32(list.length, 0);
    /* Popping an empty list is not an error. */
    PB_CHECK_U32(pb_ilist_pop_front(&list), PB_ILIST_NIL);
    PB_CHECK_U32(pb_ilist_pop_back(&list), PB_ILIST_NIL);

    pb_ilist_destroy(&list, NULL);
}

static void test_ilist_move_to_front(void)
{
    pb_ilist list;
    uint32_t node;
    uint32_t seen[4];
    int count = 0;

    PB_CHECK(pb_ilist_init(&list, 4, NULL));
    pb_ilist_push_back(&list, 0);
    pb_ilist_push_back(&list, 1);
    pb_ilist_push_back(&list, 2);
    pb_ilist_push_back(&list, 3);

    /* The LRU hit path: touch the tail, it becomes the head. */
    pb_ilist_move_to_front(&list, 3);
    PB_CHECK_U32(list.head, 3);
    PB_CHECK_U32(list.tail, 2);
    PB_CHECK_U32(list.length, 4);

    /* Moving the head to the front is a no-op, not a corruption. */
    pb_ilist_move_to_front(&list, 3);
    PB_CHECK_U32(list.head, 3);
    PB_CHECK_U32(list.length, 4);

    pb_ilist_move_to_back(&list, 3);
    PB_CHECK_U32(list.tail, 3);
    PB_CHECK_U32(list.head, 0);

    for (node = list.head; node != PB_ILIST_NIL; node = list.next[node]) {
        if (count < 4) {
            seen[count] = node;
        }
        count += 1;
    }
    PB_CHECK(count == 4);
    PB_CHECK_U32(seen[0], 0);
    PB_CHECK_U32(seen[1], 1);
    PB_CHECK_U32(seen[2], 2);
    PB_CHECK_U32(seen[3], 3);

    pb_ilist_clear(&list);
    PB_CHECK_U32(list.length, 0);
    PB_CHECK(!pb_ilist_contains(&list, 0));

    pb_ilist_destroy(&list, NULL);
}

static void test_ilist_allocation_is_confined_to_init(void)
{
    counting_allocator state = { 0, 0, 0 };
    pb_allocator allocator;
    pb_ilist list;
    uint32_t i;

    allocator.alloc = counting_alloc;
    allocator.free = counting_free;
    allocator.ctx = &state;

    PB_CHECK(pb_ilist_init(&list, 64, &allocator));
    PB_CHECK(state.allocations == 2); /* next[] and prev[] */

    for (i = 0; i < 64; ++i) {
        pb_ilist_push_front(&list, i);
    }
    for (i = 0; i < 64; ++i) {
        pb_ilist_move_to_back(&list, i);
    }
    while (pb_ilist_pop_front(&list) != PB_ILIST_NIL) {
        /* drain */
    }
    /* Not one allocation after init, which is the contract. */
    PB_CHECK(state.allocations == 2);

    PB_CHECK(pb_ilist_memory_bytes(&list) == 64u * sizeof(uint32_t) * 2u);

    pb_ilist_destroy(&list, &allocator);
    PB_CHECK(state.frees == 2);
    PB_CHECK(state.live_bytes == 0);
}

static void test_ilist_rejects_bad_capacity(void)
{
    pb_ilist list;
    PB_CHECK(!pb_ilist_init(&list, 0, NULL));
    /* A failed init still leaves a destroyable, zeroed list. */
    pb_ilist_destroy(&list, NULL);
}

/* --- pb_heap --------------------------------------------------------------- */

static void test_heap_orders_by_key(void)
{
    pb_heap heap;
    uint64_t key;
    uint32_t item;

    PB_CHECK(pb_heap_init(&heap, 16, NULL));
    PB_CHECK(!pb_heap_pop_min(&heap, &key, &item));
    PB_CHECK(!pb_heap_peek_min(&heap, &key, &item));

    PB_CHECK(pb_heap_push(&heap, 50, 5));
    PB_CHECK(pb_heap_push(&heap, 10, 1));
    PB_CHECK(pb_heap_push(&heap, 30, 3));
    PB_CHECK(pb_heap_push(&heap, 20, 2));
    PB_CHECK(pb_heap_push(&heap, 40, 4));
    PB_CHECK_U32(heap.size, 5);

    PB_CHECK(pb_heap_peek_min(&heap, &key, &item));
    PB_CHECK_U64(key, 10);
    PB_CHECK_U32(item, 1);
    PB_CHECK_U32(heap.size, 5); /* peek does not remove */

    PB_CHECK(pb_heap_pop_min(&heap, &key, &item));
    PB_CHECK_U64(key, 10);
    PB_CHECK(pb_heap_pop_min(&heap, &key, &item));
    PB_CHECK_U64(key, 20);
    PB_CHECK(pb_heap_pop_min(&heap, &key, &item));
    PB_CHECK_U64(key, 30);
    PB_CHECK(pb_heap_pop_min(&heap, &key, &item));
    PB_CHECK_U64(key, 40);
    PB_CHECK(pb_heap_pop_min(&heap, &key, &item));
    PB_CHECK_U64(key, 50);
    PB_CHECK(!pb_heap_pop_min(&heap, &key, &item));

    /* NULL out-parameters are allowed. */
    PB_CHECK(pb_heap_push(&heap, 7, 7));
    PB_CHECK(pb_heap_pop_min(&heap, NULL, NULL));

    pb_heap_destroy(&heap, NULL);
}

static void test_heap_breaks_ties_by_lower_item(void)
{
    /*
     * This is the registry's tie-breaking rule. Without a
     * total order, two languages could evict different entries from the same
     * state and both be "right".
     */
    pb_heap heap;
    uint64_t key;
    uint32_t item;

    PB_CHECK(pb_heap_init(&heap, 8, NULL));
    PB_CHECK(pb_heap_push(&heap, 100, 9));
    PB_CHECK(pb_heap_push(&heap, 100, 2));
    PB_CHECK(pb_heap_push(&heap, 100, 7));
    PB_CHECK(pb_heap_push(&heap, 100, 4));

    PB_CHECK(pb_heap_pop_min(&heap, &key, &item));
    PB_CHECK_U32(item, 2);
    PB_CHECK(pb_heap_pop_min(&heap, &key, &item));
    PB_CHECK_U32(item, 4);
    PB_CHECK(pb_heap_pop_min(&heap, &key, &item));
    PB_CHECK_U32(item, 7);
    PB_CHECK(pb_heap_pop_min(&heap, &key, &item));
    PB_CHECK_U32(item, 9);

    pb_heap_destroy(&heap, NULL);
}

static void test_heap_sorts_a_scrambled_run(void)
{
    /* Exercise sift-up and sift-down past the first level of a 4-ary heap. */
    pb_heap heap;
    uint64_t previous_key = 0;
    uint32_t i;
    int popped = 0;

    PB_CHECK(pb_heap_init(&heap, 200, NULL));
    for (i = 0; i < 200u; ++i) {
        /* A simple scramble: stride through the range co-prime to 200. */
        PB_CHECK(pb_heap_push(&heap, (uint64_t)((i * 137u) % 200u), i));
    }
    PB_CHECK(!pb_heap_push(&heap, 1, 1)); /* full */

    for (;;) {
        uint64_t key;
        if (!pb_heap_pop_min(&heap, &key, NULL)) {
            break;
        }
        PB_CHECK(key >= previous_key);
        previous_key = key;
        popped += 1;
    }
    PB_CHECK(popped == 200);

    pb_heap_clear(&heap);
    PB_CHECK_U32(heap.size, 0);
    pb_heap_destroy(&heap, NULL);
}

static void test_heap_allocation_is_confined_to_init(void)
{
    counting_allocator state = { 0, 0, 0 };
    pb_allocator allocator;
    pb_heap heap;
    uint32_t i;

    allocator.alloc = counting_alloc;
    allocator.free = counting_free;
    allocator.ctx = &state;

    PB_CHECK(pb_heap_init(&heap, 32, &allocator));
    PB_CHECK(state.allocations == 2);

    for (i = 0; i < 32u; ++i) {
        PB_CHECK(pb_heap_push(&heap, 32u - i, i));
    }
    while (pb_heap_pop_min(&heap, NULL, NULL)) {
        /* drain */
    }
    PB_CHECK(state.allocations == 2);

    pb_heap_destroy(&heap, &allocator);
    PB_CHECK(state.live_bytes == 0);
}

/* --- pb_ring --------------------------------------------------------------- */

static void test_ring_wraps(void)
{
    pb_ring ring;
    uint32_t i;

    PB_CHECK(pb_ring_init(&ring, 4, NULL));
    PB_CHECK(pb_ring_is_empty(&ring));
    PB_CHECK_U32(pb_ring_pop_front(&ring), PB_RING_NIL);
    PB_CHECK_U32(pb_ring_peek_front(&ring), PB_RING_NIL);

    PB_CHECK(pb_ring_push_back(&ring, 10));
    PB_CHECK(pb_ring_push_back(&ring, 11));
    PB_CHECK(pb_ring_push_back(&ring, 12));
    PB_CHECK(pb_ring_push_back(&ring, 13));
    PB_CHECK(pb_ring_is_full(&ring));
    PB_CHECK(!pb_ring_push_back(&ring, 14)); /* full, and says so */

    PB_CHECK_U32(pb_ring_peek_front(&ring), 10);
    PB_CHECK_U32(pb_ring_at(&ring, 0), 10);
    PB_CHECK_U32(pb_ring_at(&ring, 3), 13);
    PB_CHECK_U32(pb_ring_at(&ring, 4), PB_RING_NIL);

    /* Drain two and refill, forcing the indices to wrap around the array. */
    PB_CHECK_U32(pb_ring_pop_front(&ring), 10);
    PB_CHECK_U32(pb_ring_pop_front(&ring), 11);
    PB_CHECK(pb_ring_push_back(&ring, 14));
    PB_CHECK(pb_ring_push_back(&ring, 15));
    PB_CHECK(pb_ring_is_full(&ring));

    PB_CHECK_U32(pb_ring_at(&ring, 0), 12);
    PB_CHECK_U32(pb_ring_at(&ring, 1), 13);
    PB_CHECK_U32(pb_ring_at(&ring, 2), 14);
    PB_CHECK_U32(pb_ring_at(&ring, 3), 15);

    for (i = 12; i <= 15u; ++i) {
        PB_CHECK_U32(pb_ring_pop_front(&ring), i);
    }
    PB_CHECK(pb_ring_is_empty(&ring));

    pb_ring_clear(&ring);
    PB_CHECK(pb_ring_is_empty(&ring));
    PB_CHECK(pb_ring_memory_bytes(&ring) == 4u * sizeof(uint32_t));

    pb_ring_destroy(&ring, NULL);
}

static void test_ring_survives_many_wraps(void)
{
    pb_ring ring;
    uint32_t i;

    PB_CHECK(pb_ring_init(&ring, 3, NULL));
    for (i = 0; i < 1000u; ++i) {
        PB_CHECK(pb_ring_push_back(&ring, i));
        PB_CHECK_U32(pb_ring_pop_front(&ring), i);
    }
    PB_CHECK(pb_ring_is_empty(&ring));
    pb_ring_destroy(&ring, NULL);
}

static void test_ring_rejects_bad_capacity(void)
{
    pb_ring ring;
    PB_CHECK(!pb_ring_init(&ring, 0, NULL));
    pb_ring_destroy(&ring, NULL);
}

/* --- pb_fnv1a64 ------------------------------------------------------------ */

static void test_fnv1a64(void)
{
    /* Published FNV-1a 64 test vectors. */
    PB_CHECK_U64(pb_fnv1a64_str(""), 0xcbf29ce484222325ULL);
    PB_CHECK_U64(pb_fnv1a64_str("a"), 0xaf63dc4c8601ec8cULL);
    PB_CHECK_U64(pb_fnv1a64_str("foobar"), 0x85944171f73967e8ULL);
    /* The string and buffer forms must agree. */
    PB_CHECK_U64(pb_fnv1a64("foobar", 6), pb_fnv1a64_str("foobar"));
}

/* --- pb_map ---------------------------------------------------------------- */

static void test_map_basics(void)
{
    pb_map map;
    uint32_t value = 0;

    PB_CHECK(pb_map_init(&map, 16, NULL));
    PB_CHECK(map.count == 0u);
    /* Sized for 16 entries at 50% load, so at least 32 slots. */
    PB_CHECK(map.capacity >= 32u);

    PB_CHECK(!pb_map_get(&map, 42, &value));
    PB_CHECK(pb_map_put(&map, 42, 7));
    PB_CHECK(pb_map_get(&map, 42, &value));
    PB_CHECK_U32(value, 7);
    PB_CHECK(map.count == 1u);

    /* Putting an existing key updates rather than duplicating. */
    PB_CHECK(pb_map_put(&map, 42, 9));
    PB_CHECK(map.count == 1u);
    PB_CHECK(pb_map_get(&map, 42, &value));
    PB_CHECK_U32(value, 9);

    /* A NULL out-parameter is allowed. */
    PB_CHECK(pb_map_get(&map, 42, NULL));

    PB_CHECK(pb_map_remove(&map, 42));
    PB_CHECK(!pb_map_get(&map, 42, &value));
    PB_CHECK(map.count == 0u);
    PB_CHECK(!pb_map_remove(&map, 42));

    /* Key 0 is an ordinary key, not a sentinel. */
    PB_CHECK(pb_map_put(&map, 0, 5));
    PB_CHECK(pb_map_get(&map, 0, &value));
    PB_CHECK_U32(value, 5);
    PB_CHECK(pb_map_put(&map, UINT64_MAX, 6));
    PB_CHECK(pb_map_get(&map, UINT64_MAX, &value));
    PB_CHECK_U32(value, 6);

    pb_map_clear(&map);
    PB_CHECK(map.count == 0u);
    PB_CHECK(!pb_map_get(&map, 0, &value));

    pb_map_destroy(&map, NULL);
}

static void test_map_survives_interleaved_removal(void)
{
    /*
     * The property backward-shift deletion exists to protect: after arbitrary
     * removals, every key still present must still be findable. A tombstone
     * bug, or a shift that moves an entry out of its probe chain, shows up
     * here and essentially nowhere else.
     */
    enum { KEY_COUNT = 400 };
    pb_map map;
    pb_rng rng;
    uint64_t keys[KEY_COUNT];
    bool present[KEY_COUNT];
    uint32_t value = 0;
    uint32_t i;
    uint32_t round;
    uint32_t live = 0;

    PB_CHECK(pb_map_init(&map, KEY_COUNT, NULL));
    pb_rng_init(&rng, 1234u);

    /*
     * Keys that stress the hash: the index appears in both the high and low
     * 16 bits, so a hash that only looks at one half piles them into the same
     * buckets. The construction is injective for i < 65536, which the check
     * below enforces — duplicate keys would make this test fail in a way that
     * looks exactly like a map bug.
     */
    for (i = 0; i < (uint32_t)KEY_COUNT; ++i) {
        keys[i] = ((uint64_t)i << 16) | (uint64_t)i;
        present[i] = false;
    }
    for (i = 1; i < (uint32_t)KEY_COUNT; ++i) {
        uint32_t j;
        for (j = 0; j < i; ++j) {
            if (keys[i] == keys[j]) {
                PB_CHECK(false); /* the fixture is wrong, not the map */
            }
        }
    }

    for (round = 0; round < 20u; ++round) {
        for (i = 0; i < (uint32_t)KEY_COUNT; ++i) {
            bool should_insert = pb_rng_next_int(&rng, 2u) == 0u;

            if (should_insert && !present[i]) {
                PB_CHECK(pb_map_put(&map, keys[i], i));
                present[i] = true;
                live += 1u;
            } else if (!should_insert && present[i]) {
                PB_CHECK(pb_map_remove(&map, keys[i]));
                present[i] = false;
                live -= 1u;
            }
        }

        /* Everything inserted is still findable, with the right value, and
         * nothing removed has come back. */
        PB_CHECK(map.count == live);
        for (i = 0; i < (uint32_t)KEY_COUNT; ++i) {
            if (present[i]) {
                if (!pb_map_get(&map, keys[i], &value) || value != i) {
                    PB_CHECK(false);
                    break;
                }
            } else if (pb_map_get(&map, keys[i], NULL)) {
                PB_CHECK(false);
                break;
            }
        }
    }

    pb_map_destroy(&map, NULL);
}

static void test_map_allocation_is_confined_to_init(void)
{
    counting_allocator state = { 0, 0, 0 };
    pb_allocator allocator;
    pb_map map;
    uint32_t i;

    allocator.alloc = counting_alloc;
    allocator.free = counting_free;
    allocator.ctx = &state;

    PB_CHECK(pb_map_init(&map, 64, &allocator));
    PB_CHECK(state.allocations == 3); /* keys, values, occupied */

    for (i = 0; i < 64u; ++i) {
        PB_CHECK(pb_map_put(&map, (uint64_t)i * 7u, i));
    }
    for (i = 0; i < 64u; ++i) {
        PB_CHECK(pb_map_remove(&map, (uint64_t)i * 7u));
    }
    /* No rehash, no growth, no tombstone cleanup — nothing allocates. */
    PB_CHECK(state.allocations == 3);
    PB_CHECK(pb_map_memory_bytes(&map) > 0u);

    pb_map_destroy(&map, &allocator);
    PB_CHECK(state.frees == 3);
    PB_CHECK(state.live_bytes == 0);
}

static void test_map_rejects_zero_capacity(void)
{
    pb_map map;
    PB_CHECK(!pb_map_init(&map, 0, NULL));
    pb_map_destroy(&map, NULL);
}

/* A full table refuses a new key rather than probing forever. */
static void test_map_full_table_refuses_a_new_key(void)
{
    pb_map map;
    uint64_t key;
    uint32_t value = 0;

    PB_CHECK(pb_map_init(&map, 4, NULL));
    PB_CHECK_U32(map.capacity, 8);

    /* Fill every slot. The sizing rule keeps callers at 50% load, but the map
     * itself accepts inserts right up to a full table. */
    for (key = 0; key < 8u; ++key) {
        PB_CHECK(pb_map_put(&map, key, (uint32_t)key));
    }
    PB_CHECK_U32(map.count, 8);

    /* A ninth distinct key has no empty slot to land in. Every probe would
     * find an occupied slot, so the walk must be bounded by the table. */
    PB_CHECK(!pb_map_put(&map, 99u, 99u));
    PB_CHECK_U32(map.count, 8);

    /* A resident key is still found and still updatable. */
    PB_CHECK(pb_map_put(&map, 3u, 33u));
    PB_CHECK(pb_map_get(&map, 3u, &value));
    PB_CHECK_U32(value, 33);

    pb_map_destroy(&map, NULL);
}

/* A request no uint32-indexed table can hold fails in sizing, not in malloc. */
static void test_map_rejects_impossible_sizes(void)
{
    counting_allocator state = { 0, 0, 0 };
    pb_allocator allocator = { counting_alloc, counting_free, &state };
    pb_map map;

    /* From 2^31 entries up, `min_entries * 2` wraps a uint32 to a small
     * number and would size an eight-slot table for two billion keys. */
    PB_CHECK(!pb_map_init(&map, 0x80000000u, &allocator));
    PB_CHECK(!pb_map_init(&map, UINT32_MAX, &allocator));
    PB_CHECK(state.allocations == 0);
}

int main(void)
{
    test_ilist_basics();
    test_ilist_move_to_front();
    test_ilist_allocation_is_confined_to_init();
    test_ilist_rejects_bad_capacity();

    test_heap_orders_by_key();
    test_heap_breaks_ties_by_lower_item();
    test_heap_sorts_a_scrambled_run();
    test_heap_allocation_is_confined_to_init();

    test_ring_wraps();
    test_ring_survives_many_wraps();
    test_ring_rejects_bad_capacity();

    test_map_basics();
    test_map_survives_interleaved_removal();
    test_map_allocation_is_confined_to_init();
    test_map_rejects_zero_capacity();
    test_map_full_table_refuses_a_new_key();
    test_map_rejects_impossible_sizes();

    test_fnv1a64();

    return pb_test_summary("test_ds");
}
