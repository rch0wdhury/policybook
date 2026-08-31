/*
 * GENERATED FILE — do not edit.
 *
 * Produced by scripts/gen-c-trace-parity.ts from
 * packages/core/src/domains/kv-cache/trace-prefix.json. Regenerate with:
 *
 *     pnpm tsx scripts/gen-c-trace-parity.ts
 *
 * The first 10,000 events of every canonical kv-cache trace, as
 * produced by the reference TypeScript generator. The C generator has to
 * reproduce them exactly, which is what makes C benchmark numbers comparable
 * with the canonical ones (concept.md §12.1, §12.2).
 */

#include <stddef.h>
#include <stdint.h>

#include <string.h>

#include "policybook/allocator.h"
#include "policybook/kv_cache/traces.h"

#include "../pb_test.h"

/* The first 10 steps of decode-4096, as float32 bit patterns. */
static const uint32_t expected_decode_4096_bits[] = {
    1065353216u, 1057091522u, 1056710779u, 1051789414u, 1051372203u, 1050954991u, 1049169913u, 1048773971u,
    1048180058u, 1047388173u, 1046887454u, 1046054006u, 1045220557u, 1044387108u, 1043553659u, 1044968671u,
    1044107532u, 1043246392u, 1042385253u, 1041524113u, 1041669607u, 1043597057u, 1042716155u, 1041835253u,
    1040954350u, 1039959505u, 1040199179u, 1040324910u, 1042567400u, 1041671690u, 1040775981u, 1039573152u,
    1037781733u, 1038003581u, 1038225428u, 1038447276u, 1041765693u, 1040858481u, 1039715144u, 1037900720u,
    1036086295u, 1036285136u, 1036483977u, 1036682818u, 1036881659u, 1041123532u, 1040207130u, 1038394063u,
    1036561258u, 1034728452u, 1034908913u, 1035089374u, 1035269835u, 1035450296u, 1035630757u,
};

#define EXPECTED_DECODE_4096_STEPS 10

static const uint32_t expected_decode_4096_hash = 3136039440u;

/*
 * Compare a generated trace against the reference, bit for bit.
 *
 * Step t holds t weights, so the expected bits are one flat array walked
 * with a running offset. Only the first divergence is reported: once two
 * streams split, every later weight differs too.
 *
 * The hash covers every step rather than the committed first few, so a port
 * that matched for ten steps and drifted at the first heavy-hitter redraw
 * still fails — which is the case the first steps alone could not catch.
 */
static void check_trace(const char *id, const uint32_t *expected_bits, size_t steps,
                        uint32_t expected_hash)
{
    const pb_kvcache_trace_spec *spec = pb_kvcache_trace_find(id);
    pb_kvcache_trace_gen gen;
    size_t offset = 0;
    size_t step;
    size_t mismatches = 0;
    uint32_t hash;

    PB_CHECK(spec != NULL);
    if (spec == NULL) {
        return;
    }

    PB_CHECK(pb_kvcache_trace_gen_init(&gen, spec, NULL) == 0);

    for (step = 1; step <= steps; ++step) {
        size_t len = 0;
        size_t i;
        const float *weights = pb_kvcache_trace_gen_next(&gen, &len);

        PB_CHECK(weights != NULL);
        PB_CHECK(len == step);
        if (weights == NULL || len != step) {
            break;
        }

        for (i = 0; i < len; ++i) {
            uint32_t bits;
            memcpy(&bits, &weights[i], sizeof(bits));
            if (bits != expected_bits[offset + i]) {
                if (mismatches == 0) {
                    fprintf(stderr,
                            "FAIL %s diverges from the reference at step %lu, "
                            "position %lu: got bits 0x%08lx, expected 0x%08lx\n",
                            id, (unsigned long)step, (unsigned long)i,
                            (unsigned long)bits,
                            (unsigned long)expected_bits[offset + i]);
                }
                mismatches += 1;
            }
        }
        offset += len;
    }

    PB_CHECK(mismatches == 0);
    pb_kvcache_trace_gen_destroy(&gen);

    hash = pb_kvcache_trace_hash(spec, NULL);
    if (hash != expected_hash) {
        fprintf(stderr,
                "FAIL %s hashes to 0x%08lx over the whole trace, expected 0x%08lx\n",
                id, (unsigned long)hash, (unsigned long)expected_hash);
    }
    PB_CHECK(hash == expected_hash);
}

int main(void)
{
    check_trace("decode-4096", expected_decode_4096_bits,
                EXPECTED_DECODE_4096_STEPS, expected_decode_4096_hash);
    return pb_test_summary("test_kvcache_trace_parity");
}
