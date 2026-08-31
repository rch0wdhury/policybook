/*
 * pb_allocator — the only way memory is obtained in libpolicybook.
 *
 * Policies are meant to run in places where malloc is unavailable, audited, or
 * simply unwelcome: firmware, kernels, database engines. So every allocation
 * goes through a caller-supplied allocator, and every policy takes all the
 * memory it will ever need in its create function and nothing afterwards
 * (concept.md §12.2).
 *
 * Passing NULL selects malloc/free, which is the right default for a test
 * program and the wrong one for a hot path.
 */

#ifndef POLICYBOOK_ALLOCATOR_H
#define POLICYBOOK_ALLOCATOR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * An allocator.
 *
 * `free` receives the original size, so an arena or pool implementation does
 * not have to store a header alongside every block.
 */
typedef struct pb_allocator {
    void *(*alloc)(void *ctx, size_t n);
    void (*free)(void *ctx, void *p, size_t n);
    void *ctx;
} pb_allocator;

/* The malloc/free allocator. Never NULL. */
const pb_allocator *pb_allocator_default(void);

/* Allocate `n` bytes, treating a NULL allocator as the default one. */
static inline void *pb_alloc(const pb_allocator *allocator, size_t n)
{
    const pb_allocator *chosen = (allocator == NULL) ? pb_allocator_default() : allocator;
    return chosen->alloc(chosen->ctx, n);
}

/* Release a block obtained from pb_alloc with the same allocator and size. */
static inline void pb_free(const pb_allocator *allocator, void *p, size_t n)
{
    const pb_allocator *chosen = (allocator == NULL) ? pb_allocator_default() : allocator;
    chosen->free(chosen->ctx, p, n);
}

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_ALLOCATOR_H */
