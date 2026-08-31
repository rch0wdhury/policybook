#include "fuzz_cache_core.h"

#include <stdio.h>
#include <string.h>

#include "policybook/cache/2q.h"
#include "policybook/cache/arc.h"
#include "policybook/cache/cache.h"
#include "policybook/cache/clock.h"
#include "policybook/cache/fifo.h"
#include "policybook/cache/lfu.h"
#include "policybook/cache/lru.h"
#include "policybook/cache/s3_fifo.h"
#include "policybook/cache/sieve.h"
#include "policybook/cache/w_tinylfu.h"

/* Every policy takes a different params struct, so each gets a small maker. */
typedef struct pb_fuzz_policy {
    const char *name;
    const pb_cache_vtable *vtable;
    pb_cache *(*make)(uint32_t capacity);
} pb_fuzz_policy;

#define PB_FUZZ_MAKER(fn, type, macro, vt)                                                  \
    static pb_cache *fn(uint32_t capacity)                                                  \
    {                                                                                       \
        type params = macro;                                                                \
        params.capacity = capacity;                                                         \
        return vt.create(&params, NULL, NULL);                                              \
    }

PB_FUZZ_MAKER(make_fifo, pb_cache_fifo_params, PB_CACHE_FIFO_PARAMS_DEFAULT, pb_cache_fifo)
PB_FUZZ_MAKER(make_lru, pb_cache_lru_params, PB_CACHE_LRU_PARAMS_DEFAULT, pb_cache_lru)
PB_FUZZ_MAKER(make_lfu, pb_cache_lfu_params, PB_CACHE_LFU_PARAMS_DEFAULT, pb_cache_lfu)
PB_FUZZ_MAKER(make_clock, pb_cache_clock_params, PB_CACHE_CLOCK_PARAMS_DEFAULT, pb_cache_clock)
PB_FUZZ_MAKER(make_sieve, pb_cache_sieve_params, PB_CACHE_SIEVE_PARAMS_DEFAULT, pb_cache_sieve)
PB_FUZZ_MAKER(make_2q, pb_cache_2q_params, PB_CACHE_2Q_PARAMS_DEFAULT, pb_cache_2q)
PB_FUZZ_MAKER(make_arc, pb_cache_arc_params, PB_CACHE_ARC_PARAMS_DEFAULT, pb_cache_arc)
PB_FUZZ_MAKER(make_s3_fifo, pb_cache_s3_fifo_params, PB_CACHE_S3_FIFO_PARAMS_DEFAULT,
              pb_cache_s3_fifo)
PB_FUZZ_MAKER(make_w_tinylfu, pb_cache_w_tinylfu_params, PB_CACHE_W_TINYLFU_PARAMS_DEFAULT,
              pb_cache_w_tinylfu)

static const pb_fuzz_policy PB_FUZZ_POLICIES[] = {
    { "cache/fifo", &pb_cache_fifo, make_fifo },
    { "cache/lru", &pb_cache_lru, make_lru },
    { "cache/lfu", &pb_cache_lfu, make_lfu },
    { "cache/clock", &pb_cache_clock, make_clock },
    { "cache/sieve", &pb_cache_sieve, make_sieve },
    { "cache/2q", &pb_cache_2q, make_2q },
    { "cache/arc", &pb_cache_arc, make_arc },
    { "cache/s3-fifo", &pb_cache_s3_fifo, make_s3_fifo },
    { "cache/w-tinylfu", &pb_cache_w_tinylfu, make_w_tinylfu }
};

#define PB_FUZZ_POLICY_COUNT (sizeof(PB_FUZZ_POLICIES) / sizeof(PB_FUZZ_POLICIES[0]))

unsigned pb_fuzz_cache_policy_count(void)
{
    return (unsigned)PB_FUZZ_POLICY_COUNT;
}

/* A broken policy fails on nearly every input, so only the first few are
 * printed; thousands of identical lines bury the one that matters. */
#define PB_FUZZ_MAX_REPORTS 5
static int pb_fuzz_reported = 0;

static void pb_fuzz_report(int *violations, const char *policy, const char *what)
{
    *violations += 1;
    if (pb_fuzz_reported < PB_FUZZ_MAX_REPORTS) {
        fprintf(stderr, "FUZZ %s: %s\n", policy, what);
        pb_fuzz_reported += 1;
        if (pb_fuzz_reported == PB_FUZZ_MAX_REPORTS) {
            fprintf(stderr, "FUZZ: further violations suppressed\n");
        }
    }
}

int pb_fuzz_cache_once(const uint8_t *data, size_t size)
{
    const pb_fuzz_policy *chosen;
    pb_cache *cache;
    uint8_t resident[PB_FUZZ_KEY_SPACE];
    uint32_t capacity;
    uint32_t count = 0;
    size_t initial_memory;
    size_t index;
    int violations = 0;

    /* Two bytes of configuration, then one byte per access. */
    if (size < 3u) {
        return 0;
    }

    chosen = &PB_FUZZ_POLICIES[data[0] % PB_FUZZ_POLICY_COUNT];
    /* Every policy here needs at least two entries; keep caches small so the
     * interesting states — full, nearly empty, churning — are reached quickly. */
    capacity = 2u + (uint32_t)(data[1] % 30u);

    cache = chosen->make(capacity);
    if (cache == NULL) {
        return 0; /* refusing bad parameters is allowed */
    }

    memset(resident, 0, sizeof(resident));
    initial_memory = chosen->vtable->memory_bytes(cache);

    for (index = 2; index < size; ++index) {
        uint64_t key = (uint64_t)data[index];
        bool hit = resident[key] != 0u;

        chosen->vtable->on_access(cache, key, hit, NULL);
        if (hit) {
            continue;
        }

        resident[key] = 1u;
        count += 1u;

        while (count > capacity) {
            uint64_t victim = chosen->vtable->evict(cache);

            if (victim >= PB_FUZZ_KEY_SPACE) {
                pb_fuzz_report(&violations, chosen->name, "evict returned a key never inserted");
                goto done;
            }
            if (resident[victim] == 0u) {
                pb_fuzz_report(&violations, chosen->name, "evict returned a key it does not hold");
                goto done;
            }
            resident[victim] = 0u;
            count -= 1u;
        }

        if (count > capacity) {
            pb_fuzz_report(&violations, chosen->name, "resident count exceeds capacity");
            goto done;
        }
        if (chosen->vtable->memory_bytes(cache) != initial_memory) {
            pb_fuzz_report(&violations, chosen->name, "memory changed after create");
            goto done;
        }
    }

done:
    chosen->vtable->destroy(cache);
    return violations;
}
