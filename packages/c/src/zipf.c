#include "policybook/zipf.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>

double pb_zipf_weight(uint32_t rank, double alpha)
{
    double r = (double)rank + 1.0;

    if (alpha == 1.0) {
        return 1.0 / r;
    }

    /* r^0.75 = sqrt(r) * sqrt(sqrt(r)) — two correctly rounded operations. */
    {
        double s = sqrt(r);
        double q = sqrt(s);
        return 1.0 / (s * q);
    }
}

bool pb_zipf_init(pb_zipf *zipf, uint32_t size, double alpha, const pb_allocator *allocator)
{
    double running = 0.0;
    uint32_t rank;

    assert(zipf != NULL);

    zipf->cumulative = NULL;
    zipf->total = 0.0;
    zipf->size = 0;

    if (size == 0u) {
        return false;
    }
    if (alpha != 1.0 && alpha != 0.75) {
        /*
         * Other exponents would need pow, which is not correctly rounded across
         * C standard libraries and would break trace reproducibility.
         */
        return false;
    }

    zipf->cumulative = (double *)pb_alloc(allocator, (size_t)size * sizeof(double));
    if (zipf->cumulative == NULL) {
        return false;
    }

    /*
     * Summed in ascending rank order, which fixes the floating-point result.
     * Any other order gives a slightly different total, and eventually a
     * different sampled key.
     */
    for (rank = 0; rank < size; ++rank) {
        running += pb_zipf_weight(rank, alpha);
        zipf->cumulative[rank] = running;
    }

    zipf->total = running;
    zipf->size = size;
    return true;
}

void pb_zipf_destroy(pb_zipf *zipf, const pb_allocator *allocator)
{
    if (zipf == NULL || zipf->size == 0u) {
        return;
    }
    pb_free(allocator, zipf->cumulative, (size_t)zipf->size * sizeof(double));
    zipf->cumulative = NULL;
    zipf->total = 0.0;
    zipf->size = 0;
}

uint32_t pb_zipf_sample(const pb_zipf *zipf, pb_rng *rng)
{
    double target;
    uint32_t low = 0;
    uint32_t high;

    assert(zipf != NULL);
    assert(zipf->size > 0u);

    target = pb_rng_next_float(rng) * zipf->total;
    high = zipf->size - 1u;

    while (low < high) {
        uint32_t mid = (low + high) / 2u;
        if (zipf->cumulative[mid] > target) {
            high = mid;
        } else {
            low = mid + 1u;
        }
    }
    return low;
}
