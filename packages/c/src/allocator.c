#include "policybook/allocator.h"

/* The only translation unit permitted to include <stdlib.h> (concept.md §12.2). */
#include <stdlib.h>

static void *pb_default_alloc(void *ctx, size_t n)
{
    (void)ctx;
    return malloc(n);
}

static void pb_default_free(void *ctx, void *p, size_t n)
{
    (void)ctx;
    (void)n;
    free(p);
}

static const pb_allocator pb_default_allocator = {
    pb_default_alloc,
    pb_default_free,
    NULL,
};

const pb_allocator *pb_allocator_default(void)
{
    return &pb_default_allocator;
}
