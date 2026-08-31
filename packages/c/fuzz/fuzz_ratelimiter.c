/*
 * libFuzzer entry point for the rate-limiter domain.
 *
 * Build and run:
 *   CC=clang cmake -S packages/c -B build-fuzz -G Ninja -DPB_FUZZ=ON
 *   cmake --build build-fuzz
 *   ./build-fuzz/fuzz_ratelimiter -max_total_time=60
 *
 * Coverage-guided input is what finds the sequences nobody would write by hand:
 * a bucket refilled to exactly its burst and immediately drained, a window
 * counter rolled over at the instant of a request, a `retry_after` computed at
 * the millisecond a carry crosses. The invariants being checked are in
 * fuzz_ratelimiter_core.h.
 *
 * Any input that trips one is written to `fuzz/corpus/rate-limiter/` and kept
 * as a regression, so a fixed bug stays fixed.
 */

#include <stdlib.h>

#include "fuzz_ratelimiter_core.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (pb_fuzz_ratelimiter_once(data, size) != 0) {
        /* abort() rather than returning: libFuzzer records the input that got
         * here, which is the whole value of the run. */
        abort();
    }
    return 0;
}
