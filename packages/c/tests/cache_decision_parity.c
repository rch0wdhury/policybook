/*
 * The C cache ports must reproduce the committed decision streams.
 *
 * The shared vectors pin small hand-authored scenarios, and the generated
 * trace-parity test pins the generators, but neither replays a policy's
 * *decisions* over a whole trace across languages — the gap where the
 * 2Q/S3-FIFO ghost divergence lived: three ports, all green, disagreeing on
 * every canonical trace. The expected hashes are generated from the
 * TypeScript ports by scripts/gen-decision-parity.ts; this test recomputes
 * them with the C ports over the four canonical traces and two deliberately
 * churny small-cache streams, byte for byte.
 *
 * Protocol (identical in all three languages): FNV-1a64 over one outcome byte
 * per event (1 hit, 0 miss inserted, 2 miss rejected by admit), and each
 * victim key as four little-endian bytes.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "policybook/cache/2q.h"
#include "policybook/cache/arc.h"
#include "policybook/cache/clock.h"
#include "policybook/cache/fifo.h"
#include "policybook/cache/lfu.h"
#include "policybook/cache/lru.h"
#include "policybook/cache/s3_fifo.h"
#include "policybook/cache/sieve.h"
#include "policybook/cache/traces.h"
#include "policybook/cache/w_tinylfu.h"
#include "policybook/rng.h"

#include "gen/decision_parity_expected.h"
#include "pb_test.h"

#define FNV_PRIME UINT64_C(0x100000001b3)
#define FNV_OFFSET UINT64_C(0xcbf29ce484222325)

/* Mirrors CHURN_STREAMS in packages/core/src/domains/cache/decision-parity.ts. */
typedef struct churn_spec {
    const char *id;
    uint32_t seed;
    uint32_t capacity;
    uint32_t key_universe;
    uint32_t events;
} churn_spec;

static const churn_spec CHURN_STREAMS[] = {
    { "churn-small", 1u, 4u, 10u, 20000u },
    { "churn-wide", 4u, 10u, 30u, 20000u },
};

static void generate_churn(const churn_spec *spec, uint32_t *out)
{
    uint32_t state = spec->seed;
    uint32_t i;
    for (i = 0; i < spec->events; ++i) {
        state = state * 1664525u + 1013904223u;
        out[i] = state % spec->key_universe;
    }
}

typedef struct drive_result {
    uint64_t hash;
    uint64_t hits;
    uint64_t evictions;
    uint64_t rejections;
} drive_result;

static uint64_t mix(uint64_t h, uint8_t byte)
{
    return (h ^ (uint64_t)byte) * FNV_PRIME;
}

static bool drive(const pb_cache_vtable *vt, const void *params, const uint32_t *trace,
                  size_t events, uint32_t capacity, uint32_t key_universe, drive_result *out)
{
    pb_rng rng;
    pb_cache *cache;
    uint8_t *resident;
    uint32_t resident_count = 0;
    uint64_t h = FNV_OFFSET;
    size_t i;

    memset(out, 0, sizeof(*out));

    pb_rng_init(&rng, 1u);
    cache = vt->create(params, NULL, &rng);
    resident = (uint8_t *)calloc(key_universe, 1);
    if (cache == NULL || resident == NULL) {
        free(resident);
        if (cache != NULL) {
            vt->destroy(cache);
        }
        return false;
    }

    for (i = 0; i < events; ++i) {
        uint32_t key = trace[i];
        bool hit = resident[key] == 1u;
        vt->on_access(cache, (uint64_t)key, hit, NULL);

        if (hit) {
            out->hits += 1;
            h = mix(h, 1u);
            continue;
        }

        if (vt->admit != NULL && !vt->admit(cache, (uint64_t)key, NULL)) {
            out->rejections += 1;
            h = mix(h, 2u);
            continue;
        }
        h = mix(h, 0u);

        resident[key] = 1u;
        resident_count += 1u;
        while (resident_count > capacity) {
            uint64_t victim = vt->evict(cache);
            if (victim >= key_universe || resident[victim] != 1u) {
                free(resident);
                vt->destroy(cache);
                return false;
            }
            resident[victim] = 0u;
            resident_count -= 1u;
            out->evictions += 1;
            h = mix(h, (uint8_t)(victim & 0xffu));
            h = mix(h, (uint8_t)((victim >> 8) & 0xffu));
            h = mix(h, (uint8_t)((victim >> 16) & 0xffu));
            h = mix(h, (uint8_t)((victim >> 24) & 0xffu));
        }
    }

    out->hash = h;
    free(resident);
    vt->destroy(cache);
    return true;
}

/*
 * Build the params for a policy at the given capacity. Every cache policy
 * takes its defaults plus a capacity; a policy with a different shape would
 * need its own case here, which is exactly the visibility we want.
 */
static bool run_policy(const char *policy, const uint32_t *trace, size_t events,
                       uint32_t capacity, uint32_t key_universe, drive_result *out)
{
    if (strcmp(policy, "cache/2q") == 0) {
        pb_cache_2q_params p = PB_CACHE_2Q_PARAMS_DEFAULT;
        p.capacity = capacity;
        return drive(&pb_cache_2q, &p, trace, events, capacity, key_universe, out);
    }
    if (strcmp(policy, "cache/arc") == 0) {
        pb_cache_arc_params p = PB_CACHE_ARC_PARAMS_DEFAULT;
        p.capacity = capacity;
        return drive(&pb_cache_arc, &p, trace, events, capacity, key_universe, out);
    }
    if (strcmp(policy, "cache/clock") == 0) {
        pb_cache_clock_params p = PB_CACHE_CLOCK_PARAMS_DEFAULT;
        p.capacity = capacity;
        return drive(&pb_cache_clock, &p, trace, events, capacity, key_universe, out);
    }
    if (strcmp(policy, "cache/fifo") == 0) {
        pb_cache_fifo_params p = PB_CACHE_FIFO_PARAMS_DEFAULT;
        p.capacity = capacity;
        return drive(&pb_cache_fifo, &p, trace, events, capacity, key_universe, out);
    }
    if (strcmp(policy, "cache/lfu") == 0) {
        pb_cache_lfu_params p = PB_CACHE_LFU_PARAMS_DEFAULT;
        p.capacity = capacity;
        return drive(&pb_cache_lfu, &p, trace, events, capacity, key_universe, out);
    }
    if (strcmp(policy, "cache/lru") == 0) {
        pb_cache_lru_params p = PB_CACHE_LRU_PARAMS_DEFAULT;
        p.capacity = capacity;
        return drive(&pb_cache_lru, &p, trace, events, capacity, key_universe, out);
    }
    if (strcmp(policy, "cache/s3-fifo") == 0) {
        pb_cache_s3_fifo_params p = PB_CACHE_S3_FIFO_PARAMS_DEFAULT;
        p.capacity = capacity;
        return drive(&pb_cache_s3_fifo, &p, trace, events, capacity, key_universe, out);
    }
    if (strcmp(policy, "cache/sieve") == 0) {
        pb_cache_sieve_params p = PB_CACHE_SIEVE_PARAMS_DEFAULT;
        p.capacity = capacity;
        return drive(&pb_cache_sieve, &p, trace, events, capacity, key_universe, out);
    }
    if (strcmp(policy, "cache/w-tinylfu") == 0) {
        pb_cache_w_tinylfu_params p = PB_CACHE_W_TINYLFU_PARAMS_DEFAULT;
        p.capacity = capacity;
        return drive(&pb_cache_w_tinylfu, &p, trace, events, capacity, key_universe, out);
    }
    return false;
}

int main(void)
{
    /* Big enough for the largest canonical trace (zipf-0.75-1m). */
    uint32_t *trace = (uint32_t *)malloc((size_t)1000000 * sizeof(uint32_t));
    size_t i;

    if (trace == NULL) {
        fprintf(stderr, "cache decision parity: trace allocation failed\n");
        return 1;
    }

    for (i = 0; i < PB_DECISION_PARITY_COUNT; ++i) {
        const pb_decision_parity_case *expected = &PB_DECISION_PARITY[i];
        const pb_cache_trace_spec *spec = pb_cache_trace_find(expected->trace);
        drive_result actual;
        size_t events;
        uint32_t capacity;
        uint32_t key_universe;
        bool ok;

        if (spec != NULL) {
            events = pb_cache_trace_generate(spec, trace, spec->events, NULL);
            capacity = spec->capacity;
            key_universe = spec->key_universe;
            PB_CHECK(events == spec->events);
        } else {
            const churn_spec *churn = NULL;
            size_t c;
            for (c = 0; c < sizeof(CHURN_STREAMS) / sizeof(CHURN_STREAMS[0]); ++c) {
                if (strcmp(CHURN_STREAMS[c].id, expected->trace) == 0) {
                    churn = &CHURN_STREAMS[c];
                    break;
                }
            }
            PB_CHECK(churn != NULL);
            if (churn == NULL) {
                continue;
            }
            generate_churn(churn, trace);
            events = churn->events;
            capacity = churn->capacity;
            key_universe = churn->key_universe;
        }

        ok = run_policy(expected->policy, trace, events, capacity, key_universe, &actual);
        PB_CHECK(ok);
        if (!ok) {
            fprintf(stderr, "  %s on %s: drive failed\n", expected->policy, expected->trace);
            continue;
        }

        if (actual.hash != expected->hash || actual.hits != expected->hits ||
            actual.evictions != expected->evictions || actual.rejections != expected->rejections) {
            pb_test_checks += 1;
            pb_test_failures += 1;
            fprintf(stderr,
                    "FAIL %s on %s: hash %016llx vs %016llx, hits %llu vs %llu, "
                    "evictions %llu vs %llu, rejections %llu vs %llu\n",
                    expected->policy, expected->trace, (unsigned long long)actual.hash,
                    (unsigned long long)expected->hash, (unsigned long long)actual.hits,
                    (unsigned long long)expected->hits, (unsigned long long)actual.evictions,
                    (unsigned long long)expected->evictions,
                    (unsigned long long)actual.rejections,
                    (unsigned long long)expected->rejections);
        } else {
            pb_test_checks += 1;
        }
    }

    free(trace);
    return pb_test_summary("cache decision parity");
}
