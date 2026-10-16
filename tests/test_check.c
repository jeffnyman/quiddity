/*
 * test_check.c: exercises the test harness itself.
 *
 * This exists so the very first build has a test to run, proving that CTest
 * discovers and executes tests under every supported compiler. It also
 * documents, by example, how a test file is laid out.
 */

#include "check.h"

static void test_check_passes_on_true(void)
{
    CHECK(1 == 1);
    CHECK(2 > 1);
}

static void test_check_eq_int_handles_widths(void)
{
    unsigned char byte = 0xFF;
    unsigned short word = 0xFFFF;
    long negative = -1L;

    CHECK_EQ_INT(byte, 255);
    CHECK_EQ_INT(word, 65535);
    CHECK_EQ_INT(negative, -1);
}

static void test_check_eq_str_compares_contents(void)
{
    char buffer[8];
    strcpy(buffer, "zork");

    CHECK_EQ_STR(buffer, "zork");
}

int main(void)
{
    RUN_TEST(test_check_passes_on_true);
    RUN_TEST(test_check_eq_int_handles_widths);
    RUN_TEST(test_check_eq_str_compares_contents);
    return CHECK_SUMMARY();
}
