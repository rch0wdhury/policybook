/*
 * Zipf sampling for the canonical traces.
 *
 * The C side of `packages/core/src/zipf.ts`. It must produce the same ranks
 * from the same seed, draw for draw.
 *
 * Only two exponents are supported, and that is deliberate. A Zipf weight is
 * 1 / rank^alpha, which wants pow — but pow is not correctly rounded, so the
 * same call can return different doubles on different C standard libraries, and
 * a trace that differs by one ULP eventually samples a different key. The two
 * supported exponents need only sqrt, which IEEE-754 requires to be correctly
 * rounded everywhere:
 *
 *     alpha = 1.00 -> 1 / r
 *     alpha = 0.75 -> 1 / (sqrt(r) * sqrt(sqrt(r)))
 *
 * See packages/core/src/domains/cache/TRACES.md for the specification.
 */

#ifndef POLICYBOOK_ZIPF_H
#define POLICYBOOK_ZIPF_H

#include <stdbool.h>
#include <stdint.h>

#include "policybook/allocator.h"
#include "policybook/rng.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A precomputed Zipf distribution over ranks 0 .. size - 1. */
typedef struct pb_zipf {
    double *cumulative; /* ascending cumulative weights */
    double total;
    uint32_t size;
} pb_zipf;

/*
 * Build the cumulative table.
 *
 * `alpha` must be 1.0 or 0.75; anything else is refused. Returns false on a bad
 * argument or a failed allocation. This is the only function here that
 * allocates — sampling does not.
 */
bool pb_zipf_init(pb_zipf *zipf, uint32_t size, double alpha, const pb_allocator *allocator);

/* Release the table. Safe on a zeroed or already-destroyed sampler. */
void pb_zipf_destroy(pb_zipf *zipf, const pb_allocator *allocator);

/*
 * Draw a rank, consuming exactly one pb_rng_next_float.
 *
 * The count matters: a port that consumed two would diverge from the reference
 * trace on every subsequent event.
 */
uint32_t pb_zipf_sample(const pb_zipf *zipf, pb_rng *rng);

/* The weight of one rank, without calling pow. */
double pb_zipf_weight(uint32_t rank, double alpha);

#ifdef __cplusplus
}
#endif

#endif /* POLICYBOOK_ZIPF_H */
