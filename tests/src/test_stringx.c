#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <cmocka.h>

#include "tests.h"
#include "stringx.h"

static void stringx_clone_success(void** state)
{
    (void)state;

    char* dst = NULL;

    assert_int_equal(0, stringx_clone(&dst, "hello world"));
    assert_non_null(dst);
    assert_string_equal("hello world", dst);
    free(dst);
}

static void stringx_clone_empty_string_success(void** state)
{
    (void)state;

    char* dst = NULL;

    assert_int_equal(0, stringx_clone(&dst, ""));
    assert_non_null(dst);
    assert_int_equal(0, strlen(dst));
    free(dst);
}

static void stringx_clone_null_dst_fails(void** state)
{
    (void)state;

    assert_int_not_equal(0, stringx_clone(NULL, "hello"));
}

static void stringx_clone_null_src_fails(void** state)
{
    (void)state;

    char* dst = NULL;

    assert_int_not_equal(0, stringx_clone(&dst, NULL));
    assert_null(dst);
}

int test_stringx(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(stringx_clone_success),
        cmocka_unit_test(stringx_clone_empty_string_success),
        cmocka_unit_test(stringx_clone_null_dst_fails),
        cmocka_unit_test(stringx_clone_null_src_fails),
    };

    return cmocka_run_group_tests_name("stringx_tests", tests, NULL, NULL);
}
