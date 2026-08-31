#include "policybook/kv_cache/traces.h"

#include <string.h>

/*
 * Mass assigned to each component. They sum to one.
 *
 * These are the same literals the TypeScript and Python generators use. Written
 * as decimal constants rather than fractions so all three parse the identical
 * float64 value.
 */
#define PB_KVCACHE_SINK_MASS 0.15
#define PB_KVCACHE_LOCAL_MASS 0.55
#define PB_KVCACHE_HEAVY_MASS 0.25
#define PB_KVCACHE_NOISE_MASS 0.05

/* The recency window: offsets 1..64 back from the current position. */
#define PB_KVCACHE_LOCAL_SPAN 64u
/* Offset d gets weight LOCAL_DECAY - d, so 1 is heaviest and 64 lightest. */
#define PB_KVCACHE_LOCAL_DECAY 65u

/*
 * Heavy hitters are drawn from [4, t - HEAVY_MARGIN]: clear of the sinks below
 * and of the recency window above, so the three components stay separable.
 */
#define PB_KVCACHE_HEAVY_MARGIN 65u
/* Before this step the heavy component is folded into the local one. */
#define PB_KVCACHE_HEAVY_START 128u
/* The heavy set is redrawn at every multiple of this. */
#define PB_KVCACHE_HEAVY_PERIOD 512u

/* How the sink mass is split across positions 0 to 3. Sums to SINK_MASS. */
static const double pb_kvcache_sink_weights[4] = { 0.06, 0.045, 0.03, 0.015 };

const pb_kvcache_trace_spec pb_kvcache_traces[] = {
    { "decode-4096", 4096u, 7u }
};

const pb_kvcache_trace_spec *pb_kvcache_trace_find(const char *id)
{
    size_t i;

    if (id == NULL) {
        return NULL;
    }
    for (i = 0; i < (size_t)PB_KVCACHE_TRACE_COUNT; ++i) {
        if (strcmp(pb_kvcache_traces[i].id, id) == 0) {
            return &pb_kvcache_traces[i];
        }
    }
    return NULL;
}

int pb_kvcache_trace_gen_init(pb_kvcache_trace_gen *gen, const pb_kvcache_trace_spec *spec,
                              const pb_allocator *allocator)
{
    size_t n;

    if (gen == NULL || spec == NULL) {
        return -1;
    }

    memset(gen, 0, sizeof(*gen));
    gen->spec = spec;
    gen->allocator = allocator;
    gen->step = 0u;
    gen->heavy_len = 0u;
    pb_rng_init(&gen->rng, spec->seed);

    n = (size_t)spec->sequence_length;
    gen->drawn = (uint8_t *)pb_alloc(allocator, n * sizeof(uint8_t));
    gen->scratch = (double *)pb_alloc(allocator, n * sizeof(double));
    gen->weights = (float *)pb_alloc(allocator, n * sizeof(float));

    if (gen->drawn == NULL || gen->scratch == NULL || gen->weights == NULL) {
        pb_kvcache_trace_gen_destroy(gen);
        return -1;
    }
    memset(gen->drawn, 0, n * sizeof(uint8_t));
    return 0;
}

void pb_kvcache_trace_gen_destroy(pb_kvcache_trace_gen *gen)
{
    size_t n;

    if (gen == NULL || gen->spec == NULL) {
        return;
    }

    n = (size_t)gen->spec->sequence_length;
    pb_free(gen->allocator, gen->drawn, n * sizeof(uint8_t));
    pb_free(gen->allocator, gen->scratch, n * sizeof(double));
    pb_free(gen->allocator, gen->weights, n * sizeof(float));
    gen->drawn = NULL;
    gen->scratch = NULL;
    gen->weights = NULL;
    gen->spec = NULL;
}

/*
 * Draw a fresh set of heavy-hitter positions.
 *
 * Rejection sampling with a pinned call order: draw, and if the position is
 * already in the set, draw again. Both the number of draws and their order
 * matter, because the rank of a position in the set decides its weight — which
 * is why this mirrors the reference's loop exactly rather than using a cheaper
 * shuffle that would consume the stream differently.
 *
 * Membership is a flag array rather than a hash set. It is cleared by walking
 * the positions just drawn, so the cost is the set size and not the sequence
 * length.
 */
static void pb_kvcache_draw_heavy(pb_kvcache_trace_gen *gen, uint32_t t)
{
    uint32_t span;
    size_t i;

    for (i = 0; i < gen->heavy_len; ++i) {
        gen->drawn[gen->heavy[i]] = 0u;
    }
    gen->heavy_len = 0u;

    /*
     * Positions run from 4 (clear of the sinks) to t - HEAVY_MARGIN (just
     * outside the recency window, which at step t covers t-64 .. t-1), so the
     * draw is next_int(span) + 4 over that many candidates.
     */
    if (t < PB_KVCACHE_HEAVY_MARGIN + 4u) {
        return;
    }
    span = t - PB_KVCACHE_HEAVY_MARGIN - 4u + 1u;
    if (span < PB_KVCACHE_HEAVY_COUNT) {
        return;
    }

    while (gen->heavy_len < (size_t)PB_KVCACHE_HEAVY_COUNT) {
        uint32_t position = pb_rng_next_int(&gen->rng, span) + 4u;
        if (gen->drawn[position] != 0u) {
            continue;
        }
        gen->drawn[position] = 1u;
        gen->heavy[gen->heavy_len] = position;
        gen->heavy_len += 1u;
    }
}

/*
 * The attention weights for one decode step, over positions 0 .. t-1.
 *
 * Contributions accumulate in a fixed order — sink, local, heavy, noise —
 * because float addition is not associative and a different order would give a
 * different last bit. Then one division per position normalises, and one cast
 * to float rounds.
 */
static void pb_kvcache_step_weights(pb_kvcache_trace_gen *gen, uint32_t t)
{
    double *out = gen->scratch;
    uint32_t sinks;
    uint32_t span;
    uint32_t d;
    uint32_t i;
    size_t rank;
    double sink_total = 0.0;
    double local_weight = 0.0;
    double local_mass;
    double per_position;
    double total = 0.0;

    memset(out, 0, (size_t)t * sizeof(double));

    /*
     * Sinks. When fewer than four positions exist the available weights are
     * scaled so the component still contributes exactly SINK_MASS.
     */
    sinks = (t < 4u) ? t : 4u;
    for (i = 0; i < sinks; ++i) {
        sink_total += pb_kvcache_sink_weights[i];
    }
    for (i = 0; i < sinks; ++i) {
        out[i] = out[i] + (pb_kvcache_sink_weights[i] * PB_KVCACHE_SINK_MASS) / sink_total;
    }

    /* Local: offsets 1..min(64, t) back from t, weighted toward the recent. */
    span = (t < PB_KVCACHE_LOCAL_SPAN) ? t : PB_KVCACHE_LOCAL_SPAN;
    for (d = 1u; d <= span; ++d) {
        local_weight += (double)(PB_KVCACHE_LOCAL_DECAY - d);
    }

    /*
     * Below HEAVY_START the heavy component has nowhere to live, so its mass
     * joins the local one and the total still comes to one.
     */
    local_mass = (t < PB_KVCACHE_HEAVY_START)
                     ? PB_KVCACHE_LOCAL_MASS + PB_KVCACHE_HEAVY_MASS
                     : PB_KVCACHE_LOCAL_MASS;
    for (d = 1u; d <= span; ++d) {
        uint32_t position = t - d;
        out[position] =
            out[position] + ((double)(PB_KVCACHE_LOCAL_DECAY - d) * local_mass) / local_weight;
    }

    /* Heavy hitters, weighted by draw order: the first drawn is heaviest. */
    if (t >= PB_KVCACHE_HEAVY_START && gen->heavy_len > 0u) {
        double heavy_weight = 0.0;
        for (rank = 0; rank < gen->heavy_len; ++rank) {
            heavy_weight += 1.0 / (double)(rank + 1u);
        }
        for (rank = 0; rank < gen->heavy_len; ++rank) {
            uint32_t position = gen->heavy[rank];
            out[position] =
                out[position] + (PB_KVCACHE_HEAVY_MASS / (double)(rank + 1u)) / heavy_weight;
        }
    }

    /* Noise, so no position is ever exactly zero. */
    per_position = PB_KVCACHE_NOISE_MASS / (double)t;
    for (i = 0; i < t; ++i) {
        out[i] = out[i] + per_position;
    }

    /*
     * Normalise, then take each weight to float32. The total is one to within
     * rounding already, and removing this division was measured to change no
     * output bit — it stays because it makes the invariant exact in double too.
     */
    for (i = 0; i < t; ++i) {
        total += out[i];
    }
    for (i = 0; i < t; ++i) {
        gen->weights[i] = (float)(out[i] / total);
    }
}

const float *pb_kvcache_trace_gen_next(pb_kvcache_trace_gen *gen, size_t *len)
{
    uint32_t t;

    if (gen == NULL || gen->spec == NULL || gen->weights == NULL) {
        return NULL;
    }

    t = gen->step + 1u;
    /* The last step is sequence_length - 1: position 0's token never attends. */
    if (t >= gen->spec->sequence_length) {
        return NULL;
    }

    /*
     * The set is drawn when first needed and redrawn on the period. Both
     * conditions are pinned: a port that redrew on a different step would
     * diverge from here on, and the parity test would say exactly where.
     */
    if (t == PB_KVCACHE_HEAVY_START ||
        (t > PB_KVCACHE_HEAVY_START && t % PB_KVCACHE_HEAVY_PERIOD == 0u)) {
        pb_kvcache_draw_heavy(gen, t);
    }

    pb_kvcache_step_weights(gen, t);
    gen->step = t;
    if (len != NULL) {
        *len = (size_t)t;
    }
    return gen->weights;
}

#define PB_KVCACHE_FNV_OFFSET_BASIS 0x811C9DC5u
#define PB_KVCACHE_FNV_PRIME 0x01000193u

uint32_t pb_kvcache_trace_hash(const pb_kvcache_trace_spec *spec, const pb_allocator *allocator)
{
    pb_kvcache_trace_gen gen;
    uint32_t digest = PB_KVCACHE_FNV_OFFSET_BASIS;
    const float *weights;
    size_t len;

    if (pb_kvcache_trace_gen_init(&gen, spec, allocator) != 0) {
        return 0u;
    }

    while ((weights = pb_kvcache_trace_gen_next(&gen, &len)) != NULL) {
        size_t i;
        for (i = 0; i < len; ++i) {
            uint32_t bits;
            unsigned byte;
            /*
             * memcpy rather than a pointer cast: reading a float through a
             * uint32_t lvalue is undefined behaviour, and UBSan is watching.
             * Every compiler here turns this into a register move.
             */
            memcpy(&bits, &weights[i], sizeof(bits));
            /*
             * Little-endian byte order regardless of the host's, so a
             * big-endian machine hashes the same bytes in the same order.
             */
            for (byte = 0u; byte < 4u; ++byte) {
                digest ^= (bits >> (byte * 8u)) & 0xFFu;
                digest *= PB_KVCACHE_FNV_PRIME;
            }
        }
    }

    pb_kvcache_trace_gen_destroy(&gen);
    return digest;
}
