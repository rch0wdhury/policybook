/*
 * libFuzzer entry point for the kv-cache domain.
 *
 * Build and run:
 *   CC=clang cmake -S packages/c -B build-fuzz -G Ninja -DPB_FUZZ=ON
 *   cmake --build build-fuzz
 *   ./build-fuzz/fuzz_kvcache -max_total_time=60
 *
 * Coverage-guided input is what finds the sequences nobody would write by hand:
 * a cache driven to exactly its budget and held there, a recent window as wide
 * as the evictable region, an attention vector that ties every score so the
 * tie-break rule decides everything, a pyramid layer whose share is tighter
 * than the budget it is asked to meet. The invariants being checked are in
 * fuzz_kvcache_core.h.
 *
 * Any input that trips one is written to `fuzz/corpus/kv-cache/` and kept as a
 * regression, so a fixed bug stays fixed.
 */

#include <stdlib.h>

#include "fuzz_kvcache_core.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (pb_fuzz_kvcache_once(data, size) != 0) {
        /* abort() rather than returning: libFuzzer records the input that got
         * here, which is the whole value of the run. */
        abort();
    }
    return 0;
}
