/*
 * check.h: a minimal unit test harness.
 *
 * The whole harness is this header. A test file defines test functions,
 * lists them in main() with RUN_TEST, and ends with CHECK_SUMMARY. A failing
 * CHECK reports the file, line, and expression, then lets the test continue
 * so that one run shows every failure rather than only the first. The exit
 * status is non-zero when any check failed, which is all CTest needs.
 *
 *     #include "check.h"
 *
 *     static void test_arithmetic(void)
 *     {
 *         CHECK(1 + 1 == 2);
 *         CHECK_EQ_INT(6 * 7, 42);
 *     }
 *
 *     int main(void)
 *     {
 *         RUN_TEST(test_arithmetic);
 *         return CHECK_SUMMARY();
 *     }
 *
 * There is no dependency to install and nothing to learn beyond the macros
 * below, which suits a project whose purpose is to be read.
 */

#ifndef QUIDDITY_CHECK_H
#define QUIDDITY_CHECK_H

#include <stdio.h>
#include <string.h>

/* Counters are file-scope so that every test file gets its own. */
static int check_total = 0;
static int check_failed = 0;

/* Pass if the condition is true. The condition is first reduced to an int
 * variable so that a constant argument such as CHECK(1 == 1) does not
 * become a constant `if`, which MSVC reports as warning C4127. */
#define CHECK(cond)                                                       \
    do {                                                                  \
        const int check_ok_ = !!(cond);                                   \
        check_total++;                                                    \
        if (!check_ok_) {                                                 \
            check_failed++;                                               \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n",                  \
                    __FILE__, __LINE__, #cond);                           \
        }                                                                 \
    } while (0)

/* Pass if two integers are equal; on failure, show both values. Operands
 * are widened to long long so any integer type can be compared without a
 * warning. */
#define CHECK_EQ_INT(actual, expected)                                    \
    do {                                                                  \
        long long check_a_ = (long long)(actual);                         \
        long long check_e_ = (long long)(expected);                       \
        check_total++;                                                    \
        if (check_a_ != check_e_) {                                       \
            check_failed++;                                               \
            fprintf(stderr, "%s:%d: CHECK_EQ_INT failed: %s\n"            \
                    "    actual:   %lld\n"                                \
                    "    expected: %lld\n",                               \
                    __FILE__, __LINE__, #actual, check_a_, check_e_);     \
        }                                                                 \
    } while (0)

/* Pass if two C strings have identical contents. */
#define CHECK_EQ_STR(actual, expected)                                    \
    do {                                                                  \
        const char *check_a_ = (actual);                                  \
        const char *check_e_ = (expected);                                \
        check_total++;                                                    \
        if (strcmp(check_a_, check_e_) != 0) {                            \
            check_failed++;                                               \
            fprintf(stderr, "%s:%d: CHECK_EQ_STR failed: %s\n"            \
                    "    actual:   \"%s\"\n"                              \
                    "    expected: \"%s\"\n",                             \
                    __FILE__, __LINE__, #actual, check_a_, check_e_);     \
        }                                                                 \
    } while (0)

/* Run one test function, announcing it by name. */
#define RUN_TEST(fn)                                                      \
    do {                                                                  \
        printf("  %s\n", #fn);                                            \
        fn();                                                             \
    } while (0)

/* Print the totals and produce main()'s return value. */
#define CHECK_SUMMARY()                                                   \
    (printf("%d checks, %d failed\n", check_total, check_failed),         \
     check_failed == 0 ? 0 : 1)

#endif /* QUIDDITY_CHECK_H */
