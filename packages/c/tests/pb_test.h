/*
 * A test harness in thirty lines.
 *
 * The C tree carries no test framework: the generated vector tests have to be
 * readable and self-contained, and a dependency would defeat the point of a
 * library that builds anywhere with a C99 compiler.
 *
 * A test program includes this, calls the PB_CHECK macros, and ends with
 * `return pb_test_summary("name");`.
 */

#ifndef POLICYBOOK_TEST_H
#define POLICYBOOK_TEST_H

#include <inttypes.h>
#include <stdio.h>

static int pb_test_checks = 0;
static int pb_test_failures = 0;

#define PB_CHECK(cond)                                                              \
    do {                                                                            \
        pb_test_checks += 1;                                                        \
        if (!(cond)) {                                                              \
            pb_test_failures += 1;                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                           \
    } while (0)

#define PB_CHECK_U32(actual, expected)                                              \
    do {                                                                            \
        uint32_t pb_a = (uint32_t)(actual);                                         \
        uint32_t pb_e = (uint32_t)(expected);                                       \
        pb_test_checks += 1;                                                        \
        if (pb_a != pb_e) {                                                         \
            pb_test_failures += 1;                                                  \
            fprintf(stderr, "FAIL %s:%d: %s — expected %" PRIu32 ", got %" PRIu32 "\n", \
                    __FILE__, __LINE__, #actual, pb_e, pb_a);                       \
        }                                                                           \
    } while (0)

#define PB_CHECK_U64(actual, expected)                                              \
    do {                                                                            \
        uint64_t pb_a = (uint64_t)(actual);                                         \
        uint64_t pb_e = (uint64_t)(expected);                                       \
        pb_test_checks += 1;                                                        \
        if (pb_a != pb_e) {                                                         \
            pb_test_failures += 1;                                                  \
            fprintf(stderr, "FAIL %s:%d: %s — expected %" PRIu64 ", got %" PRIu64 "\n", \
                    __FILE__, __LINE__, #actual, pb_e, pb_a);                       \
        }                                                                           \
    } while (0)

/*
 * Signed comparison, for values that carry a negative sentinel.
 *
 * A retry policy returns a delay or PB_RETRY_GIVE_UP, which is -1; printing
 * that through the unsigned macro would report 18446744073709551615 and tell
 * the reader nothing.
 */
#define PB_CHECK_I64(actual, expected)                                              \
    do {                                                                            \
        int64_t pb_a = (int64_t)(actual);                                           \
        int64_t pb_e = (int64_t)(expected);                                         \
        pb_test_checks += 1;                                                        \
        if (pb_a != pb_e) {                                                         \
            pb_test_failures += 1;                                                  \
            fprintf(stderr, "FAIL %s:%d: %s — expected %" PRId64 ", got %" PRId64 "\n", \
                    __FILE__, __LINE__, #actual, pb_e, pb_a);                       \
        }                                                                           \
    } while (0)

/* Exact double comparison. Every value the vectors hold is exactly representable. */
#define PB_CHECK_DOUBLE_EXACT(actual, expected)                                     \
    do {                                                                            \
        double pb_a = (double)(actual);                                             \
        double pb_e = (double)(expected);                                           \
        pb_test_checks += 1;                                                        \
        if (pb_a != pb_e) {                                                         \
            pb_test_failures += 1;                                                  \
            fprintf(stderr, "FAIL %s:%d: %s — expected %.17g, got %.17g\n",         \
                    __FILE__, __LINE__, #actual, pb_e, pb_a);                       \
        }                                                                           \
    } while (0)

/* Print the tally and return a process exit code. */
static int pb_test_summary(const char *name)
{
    if (pb_test_failures > 0) {
        fprintf(stderr, "%s: %d of %d checks FAILED\n", name, pb_test_failures,
                pb_test_checks);
        return 1;
    }
    printf("%s: %d checks passed\n", name, pb_test_checks);
    return 0;
}

#endif /* POLICYBOOK_TEST_H */
