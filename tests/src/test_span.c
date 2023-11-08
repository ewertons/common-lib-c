#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

#include "span.h"
#include "test_span.h"

static void span_init_success(void** state)
{
    (void)state;
    uint8_t data[8];

    span_t s = span_init(data, 8);

    assert_int_equal(span_get_size(s), 8);
    assert_ptr_equal(span_get_ptr(s), data);
}

static void span_from_str_literal_success(void** state)
{
    (void)state;

    span_t s = span_from_str_literal("abcdef");

    assert_int_equal(span_get_size(s), 6);
    assert_non_null(span_get_ptr(s));
    assert_memory_equal(span_get_ptr(s), "abcdef", span_get_size(s));
}

static void span_from_memory_success(void** state)
{
    (void)state;
    uint8_t data[8] = { 1, 2, 3, 4, 3, 2, 1, 0 };

    span_t s = span_from_memory(data);

    assert_int_equal(span_get_size(s), sizeofarray(data));
    assert_ptr_equal(span_get_ptr(s), data);
    assert_memory_equal(span_get_ptr(s), data, span_get_size(s));
}

static void span_slice_success(void** state)
{
    (void)state;
    uint8_t data[8] = { '1', '2', '3', '4', '3', '2', '1', '0' };

    span_t d = span_from_memory(data);
    span_t s;

    s = span_slice(d, 0, 3);
    assert_int_equal(span_get_size(s), 3);
    assert_ptr_equal(span_get_ptr(s), span_get_ptr(d));
    assert_memory_equal(span_get_ptr(s), "123", 3);

    s = span_slice(d, 3, 4);
    assert_int_equal(span_get_size(s), 4);
    assert_ptr_equal(span_get_ptr(s), span_get_ptr(d) + 3);
    assert_memory_equal(span_get_ptr(s), "4321", 4);
}

static void span_slice_failure(void** state)
{
    (void)state;
    uint8_t data[8] = { '1', '2', '3', '4', '3', '2', '1', '0' };

    span_t d = span_from_memory(data);
    span_t s;

    s = span_slice(d, 0, 9);
    assert_int_equal(span_get_size(s), 0);
    assert_ptr_equal(span_get_ptr(s), NULL);

    s = span_slice(d, 5, 4);
    assert_int_equal(span_get_size(s), 0);
    assert_ptr_equal(span_get_ptr(s), NULL);

    s = span_slice(d, 8, 1);
    assert_int_equal(span_get_size(s), 0);
    assert_ptr_equal(span_get_ptr(s), NULL);

    s = span_slice(d, 0, 0);
    assert_int_equal(span_get_size(s), 0);
    assert_ptr_equal(span_get_ptr(s), NULL);
}

static void span_slice_to_end_success(void** state)
{
    (void)state;
    uint8_t data[8] = { '1', '2', '3', '4', '3', '2', '1', '0' };

    span_t d = span_from_memory(data);
    span_t s;

    s = span_slice_to_end(d, 0);
    assert_int_equal(span_get_size(s), span_get_size(d));
    assert_ptr_equal(span_get_ptr(s), span_get_ptr(d));

    s = span_slice_to_end(d, 3);
    assert_int_equal(span_get_size(s), 5);
    assert_ptr_equal(span_get_ptr(s), span_get_ptr(d) + 3);
    assert_memory_equal(span_get_ptr(s), "43210", 5);
}

static void span_slice_to_end_failure(void** state)
{
    (void)state;
    uint8_t data[8] = { '1', '2', '3', '4', '3', '2', '1', '0' };

    span_t d = span_from_memory(data);
    span_t s;

    s = span_slice_to_end(d, 8);
    assert_int_equal(span_get_size(s), 0);
    assert_ptr_equal(span_get_ptr(s), NULL);
}

static void span_find_success(void** state)
{
    (void)state;
    uint8_t data[8] = { '1', '2', '3', '4', '3', '2', '1', '0' };

    span_t s = span_from_memory(data);
    span_t f;
    
    assert_int_equal(span_find(s, 0, span_from_str_literal("1"), &f), 0);
    assert_int_equal(span_get_size(f), 1);
    assert_ptr_equal(span_get_ptr(f), &data[0]);

    assert_int_equal(span_find(s, 1, span_from_str_literal("1"), &f), 0);
    assert_int_equal(span_get_size(f), 1);
    assert_ptr_equal(span_get_ptr(f), &data[6]);

    assert_int_equal(span_find(s, 0, span_from_str_literal("43"), &f), 0);
    assert_int_equal(span_get_size(f), 2);
    assert_ptr_equal(span_get_ptr(f), &data[3]);
    assert_memory_equal(span_get_ptr(f), "43", 2);
}

int test_span()
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(span_init_success),
      cmocka_unit_test(span_from_str_literal_success),
      cmocka_unit_test(span_from_memory_success),
      cmocka_unit_test(span_slice_success),
      cmocka_unit_test(span_slice_failure),
      cmocka_unit_test(span_slice_to_end_success),
      cmocka_unit_test(span_find_success)
  };

  return cmocka_run_group_tests_name("span_tests", tests, NULL, NULL);
}
