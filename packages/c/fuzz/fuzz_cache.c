/*
 * libFuzzer entry point for the cache domain.
 *
 * Build and run:
 *   CC=clang cmake -S packages/c -B build-fuzz -G Ninja -DPB_FUZZ=ON
 *   cmake --build build-fuzz
 *   ./build-fuzz/fuzz_cache -max_total_time=60
 *
 * Coverage-guided input is what finds the sequences nobody would write by hand:
 * a cache driven to exactly capacity and back, a key evicted and immediately
 * re-requested, a ghost queue wrapping mid-promotion. The invariants being
 * checked are in fuzz_cache_core.h.
 *
 * Any input that trips one is written to `fuzz/corpus/cache/` and kept as a
 * regression, so a fixed bug stays fixed.
 */

#include <stdlib.h>

#include "fuzz_cache_core.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (pb_fuzz_cache_once(data, size) != 0) {
        /* abort() rather than returning: libFuzzer records the input that got
         * here, which is the whole value of the run. */
        abort();
    }
    return 0;
}
