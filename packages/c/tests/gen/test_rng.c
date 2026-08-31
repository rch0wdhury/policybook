/*
 * GENERATED FILE — do not edit.
 *
 * Produced by scripts/gen-c-rng-vectors.ts from
 * packages/core/src/rng.vectors.json. Regenerate with:
 *
 *     pnpm tsx scripts/gen-c-rng-vectors.ts
 *
 * Generator under test: xoshiro128** seeded by splitmix32
 *
 * This is the proof that the C generator agrees with the TypeScript and
 * Python ones, bit for bit, which is what makes every seeded policy and
 * trace reproducible across languages.
 */

#include <stddef.h>
#include <stdint.h>

#include "policybook/rng.h"

#include "../pb_test.h"

static void test_seed_1(void)
{
    pb_rng rng;
    size_t i;

    static const uint32_t expected_u32[] = {
        393288148u, 2174103013u, 3814759091u, 2092745082u, 1865176206u, 2179171167u,
        3207394750u, 2858353069u, 559075315u, 3395495274u, 4035540825u, 1929427096u,
        4080585408u, 498941776u, 2789075627u, 3924381082u, 891783897u, 212198729u,
        2051990826u, 3015381799u, 1303255859u, 723616818u, 2707487543u, 1195870907u,
        1493934442u, 1990658952u, 3126471170u, 464135096u, 2692946770u, 784784495u,
        3641130122u, 3172118176u,
    };
    pb_rng_init(&rng, 1u);
    for (i = 0; i < sizeof(expected_u32) / sizeof(expected_u32[0]); ++i) {
        PB_CHECK_U32(pb_rng_next_u32(&rng), expected_u32[i]);
    }

    static const double expected_float[] = {
        0.09156953264027834, 0.5061978038866073, 0.8881928145419806, 0.4872551844455302,
        0.43427017657086253, 0.5073778254445642, 0.7467797840945423, 0.6655121848452836,
    };
    pb_rng_init(&rng, 1u);
    for (i = 0; i < sizeof(expected_float) / sizeof(expected_float[0]); ++i) {
        PB_CHECK_DOUBLE_EXACT(pb_rng_next_float(&rng), expected_float[i]);
    }

    static const uint32_t expected_int_2[] = {
        0u, 1u, 1u, 0u, 0u, 1u, 0u, 1u,
        1u, 0u, 1u, 0u, 0u, 0u, 1u, 0u,
    };
    pb_rng_init(&rng, 1u);
    for (i = 0; i < sizeof(expected_int_2) / sizeof(expected_int_2[0]); ++i) {
        PB_CHECK_U32(pb_rng_next_int(&rng, 2u), expected_int_2[i]);
    }

    static const uint32_t expected_int_7[] = {
        1u, 5u, 3u, 1u, 5u, 5u, 0u, 5u,
        1u, 3u, 1u, 2u, 4u, 4u, 2u, 6u,
    };
    pb_rng_init(&rng, 1u);
    for (i = 0; i < sizeof(expected_int_7) / sizeof(expected_int_7[0]); ++i) {
        PB_CHECK_U32(pb_rng_next_int(&rng, 7u), expected_int_7[i]);
    }

    static const uint32_t expected_int_100[] = {
        48u, 13u, 91u, 82u, 6u, 67u, 50u, 69u,
        15u, 74u, 25u, 96u, 8u, 76u, 27u, 82u,
    };
    pb_rng_init(&rng, 1u);
    for (i = 0; i < sizeof(expected_int_100) / sizeof(expected_int_100[0]); ++i) {
        PB_CHECK_U32(pb_rng_next_int(&rng, 100u), expected_int_100[i]);
    }

    static const uint32_t expected_int_1000000[] = {
        288148u, 103013u, 759091u, 745082u, 176206u, 171167u, 394750u, 353069u,
        75315u, 495274u, 540825u, 427096u, 585408u, 941776u, 75627u, 381082u,
    };
    pb_rng_init(&rng, 1u);
    for (i = 0; i < sizeof(expected_int_1000000) / sizeof(expected_int_1000000[0]); ++i) {
        PB_CHECK_U32(pb_rng_next_int(&rng, 1000000u), expected_int_1000000[i]);
    }

}

static void test_seed_2(void)
{
    pb_rng rng;
    size_t i;

    static const uint32_t expected_u32[] = {
        4239540602u, 2755771122u, 1311900989u, 2098189563u, 1724675806u, 2240250650u,
        1076011064u, 2220000027u, 1614891450u, 438663176u, 269150769u, 2351779560u,
        2006604691u, 1017409403u, 4080748652u, 4149698948u, 1007069937u, 4205009298u,
        2655768561u, 2255116776u, 575973365u, 4000057326u, 1414668385u, 506071367u,
        1576859793u, 898581193u, 1275734580u, 1674758978u, 3693175972u, 775081770u,
        432970065u, 4013682594u,
    };
    pb_rng_init(&rng, 2u);
    for (i = 0; i < sizeof(expected_u32) / sizeof(expected_u32[0]); ++i) {
        PB_CHECK_U32(pb_rng_next_u32(&rng), expected_u32[i]);
    }

    static const double expected_float[] = {
        0.9870949671603739, 0.6416279640980065, 0.30545075167901814, 0.48852282646112144,
        0.4015573780052364, 0.5215990007854998, 0.2505283486098051, 0.516884035198018,
    };
    pb_rng_init(&rng, 2u);
    for (i = 0; i < sizeof(expected_float) / sizeof(expected_float[0]); ++i) {
        PB_CHECK_DOUBLE_EXACT(pb_rng_next_float(&rng), expected_float[i]);
    }

    static const uint32_t expected_int_2[] = {
        0u, 0u, 1u, 1u, 0u, 0u, 0u, 1u,
        0u, 0u, 1u, 0u, 1u, 1u, 0u, 0u,
    };
    pb_rng_init(&rng, 2u);
    for (i = 0; i < sizeof(expected_int_2) / sizeof(expected_int_2[0]); ++i) {
        PB_CHECK_U32(pb_rng_next_int(&rng, 2u), expected_int_2[i]);
    }

    static const uint32_t expected_int_7[] = {
        3u, 6u, 0u, 1u, 0u, 1u, 2u, 0u,
        4u, 0u, 6u, 4u, 0u, 3u, 1u, 3u,
    };
    pb_rng_init(&rng, 2u);
    for (i = 0; i < sizeof(expected_int_7) / sizeof(expected_int_7[0]); ++i) {
        PB_CHECK_U32(pb_rng_next_int(&rng, 7u), expected_int_7[i]);
    }

    static const uint32_t expected_int_100[] = {
        2u, 22u, 89u, 63u, 6u, 50u, 64u, 27u,
        50u, 76u, 69u, 60u, 91u, 3u, 52u, 48u,
    };
    pb_rng_init(&rng, 2u);
    for (i = 0; i < sizeof(expected_int_100) / sizeof(expected_int_100[0]); ++i) {
        PB_CHECK_U32(pb_rng_next_int(&rng, 100u), expected_int_100[i]);
    }

    static const uint32_t expected_int_1000000[] = {
        540602u, 771122u, 900989u, 189563u, 675806u, 250650u, 11064u, 27u,
        891450u, 663176u, 150769u, 779560u, 604691u, 409403u, 748652u, 698948u,
    };
    pb_rng_init(&rng, 2u);
    for (i = 0; i < sizeof(expected_int_1000000) / sizeof(expected_int_1000000[0]); ++i) {
        PB_CHECK_U32(pb_rng_next_int(&rng, 1000000u), expected_int_1000000[i]);
    }

}

static void test_seed_42(void)
{
    pb_rng rng;
    size_t i;

    static const uint32_t expected_u32[] = {
        660444221u, 3652823732u, 77672526u, 910233633u, 2297337756u, 3786072677u,
        3123505064u, 1891482476u, 2460634111u, 3466307039u, 1235700567u, 2581809382u,
        3652642737u, 2730665958u, 1302851675u, 2977163998u, 1816716173u, 1605389910u,
        2771360340u, 3683673832u, 3560650492u, 1329528888u, 1818053963u, 1178798507u,
        1740196647u, 2684385330u, 907679367u, 147453293u, 588236482u, 1789506069u,
        4258594063u, 2973123300u,
    };
    pb_rng_init(&rng, 42u);
    for (i = 0; i < sizeof(expected_u32) / sizeof(expected_u32[0]); ++i) {
        PB_CHECK_U32(pb_rng_next_u32(&rng), expected_u32[i]);
    }

    static const double expected_float[] = {
        0.15377165307290852, 0.8504893006756902, 0.018084544222801924, 0.21193028264679015,
        0.5348906284198165, 0.8815137383062392, 0.7272476945072412, 0.440395082347095,
    };
    pb_rng_init(&rng, 42u);
    for (i = 0; i < sizeof(expected_float) / sizeof(expected_float[0]); ++i) {
        PB_CHECK_DOUBLE_EXACT(pb_rng_next_float(&rng), expected_float[i]);
    }

    static const uint32_t expected_int_2[] = {
        1u, 0u, 0u, 1u, 0u, 1u, 0u, 0u,
        1u, 1u, 1u, 0u, 1u, 0u, 1u, 0u,
    };
    pb_rng_init(&rng, 42u);
    for (i = 0; i < sizeof(expected_int_2) / sizeof(expected_int_2[0]); ++i) {
        PB_CHECK_U32(pb_rng_next_int(&rng, 2u), expected_int_2[i]);
    }

    static const uint32_t expected_int_7[] = {
        3u, 5u, 1u, 1u, 0u, 2u, 1u, 2u,
        5u, 6u, 3u, 5u, 2u, 6u, 6u, 4u,
    };
    pb_rng_init(&rng, 42u);
    for (i = 0; i < sizeof(expected_int_7) / sizeof(expected_int_7[0]); ++i) {
        PB_CHECK_U32(pb_rng_next_int(&rng, 7u), expected_int_7[i]);
    }

    static const uint32_t expected_int_100[] = {
        21u, 32u, 26u, 33u, 56u, 77u, 64u, 76u,
        11u, 39u, 67u, 82u, 37u, 58u, 75u, 98u,
    };
    pb_rng_init(&rng, 42u);
    for (i = 0; i < sizeof(expected_int_100) / sizeof(expected_int_100[0]); ++i) {
        PB_CHECK_U32(pb_rng_next_int(&rng, 100u), expected_int_100[i]);
    }

    static const uint32_t expected_int_1000000[] = {
        444221u, 823732u, 672526u, 233633u, 337756u, 72677u, 505064u, 482476u,
        634111u, 307039u, 700567u, 809382u, 642737u, 665958u, 851675u, 163998u,
    };
    pb_rng_init(&rng, 42u);
    for (i = 0; i < sizeof(expected_int_1000000) / sizeof(expected_int_1000000[0]); ++i) {
        PB_CHECK_U32(pb_rng_next_int(&rng, 1000000u), expected_int_1000000[i]);
    }

}

static void test_seed_3735928559(void)
{
    pb_rng rng;
    size_t i;

    static const uint32_t expected_u32[] = {
        3842467093u, 879304004u, 3694663928u, 2788030634u, 934155191u, 702880729u,
        3422146658u, 169081873u, 3034898518u, 3260532345u, 2050117273u, 1236536921u,
        914240050u, 2351145002u, 1248889433u, 3824328429u, 303651464u, 1749728481u,
        2222915519u, 3253059929u, 3449127433u, 3921540115u, 1767657129u, 3065267677u,
        716772984u, 2819848700u, 57563597u, 794067698u, 3951502154u, 2618686976u,
        2651696954u, 1831672577u,
    };
    pb_rng_init(&rng, 3735928559u);
    for (i = 0; i < sizeof(expected_u32) / sizeof(expected_u32[0]); ++i) {
        PB_CHECK_U32(pb_rng_next_u32(&rng), expected_u32[i]);
    }

    static const double expected_float[] = {
        0.8946440864820033, 0.20472891721874475, 0.8602309804409742, 0.6491389670409262,
        0.21749995439313352, 0.1636521725449711, 0.7967806090600789, 0.03936744132079184,
    };
    pb_rng_init(&rng, 3735928559u);
    for (i = 0; i < sizeof(expected_float) / sizeof(expected_float[0]); ++i) {
        PB_CHECK_DOUBLE_EXACT(pb_rng_next_float(&rng), expected_float[i]);
    }

    static const uint32_t expected_int_2[] = {
        1u, 0u, 0u, 0u, 1u, 1u, 0u, 1u,
        0u, 1u, 1u, 1u, 0u, 0u, 1u, 1u,
    };
    pb_rng_init(&rng, 3735928559u);
    for (i = 0; i < sizeof(expected_int_2) / sizeof(expected_int_2[0]); ++i) {
        PB_CHECK_U32(pb_rng_next_int(&rng, 2u), expected_int_2[i]);
    }

    static const uint32_t expected_int_7[] = {
        3u, 5u, 4u, 4u, 4u, 5u, 0u, 2u,
        1u, 0u, 1u, 4u, 3u, 3u, 1u, 5u,
    };
    pb_rng_init(&rng, 3735928559u);
    for (i = 0; i < sizeof(expected_int_7) / sizeof(expected_int_7[0]); ++i) {
        PB_CHECK_U32(pb_rng_next_int(&rng, 7u), expected_int_7[i]);
    }

    static const uint32_t expected_int_100[] = {
        93u, 4u, 28u, 34u, 91u, 29u, 58u, 73u,
        18u, 45u, 73u, 21u, 50u, 2u, 33u, 29u,
    };
    pb_rng_init(&rng, 3735928559u);
    for (i = 0; i < sizeof(expected_int_100) / sizeof(expected_int_100[0]); ++i) {
        PB_CHECK_U32(pb_rng_next_int(&rng, 100u), expected_int_100[i]);
    }

    static const uint32_t expected_int_1000000[] = {
        467093u, 304004u, 663928u, 30634u, 155191u, 880729u, 146658u, 81873u,
        898518u, 532345u, 117273u, 536921u, 240050u, 145002u, 889433u, 328429u,
    };
    pb_rng_init(&rng, 3735928559u);
    for (i = 0; i < sizeof(expected_int_1000000) / sizeof(expected_int_1000000[0]); ++i) {
        PB_CHECK_U32(pb_rng_next_int(&rng, 1000000u), expected_int_1000000[i]);
    }

}

static void test_mix32(void)
{
    PB_CHECK_U32(pb_mix32(0u), 0u);
    PB_CHECK_U32(pb_mix32(1u), 2261973619u);
    PB_CHECK_U32(pb_mix32(2u), 229111015u);
    PB_CHECK_U32(pb_mix32(42u), 671623878u);
    PB_CHECK_U32(pb_mix32(123456789u), 956453899u);
    PB_CHECK_U32(pb_mix32(2654435769u), 1684164658u);
    PB_CHECK_U32(pb_mix32(3735928559u), 707447538u);
    PB_CHECK_U32(pb_mix32(4294967295u), 2578835075u);
}

int main(void)
{
    test_seed_1();
    test_seed_2();
    test_seed_42();
    test_seed_3735928559();
    test_mix32();
    return pb_test_summary("test_rng");
}
