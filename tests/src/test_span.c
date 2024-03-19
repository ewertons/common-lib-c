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

    assert_int_equal(span_find(s, 1, span_from_str_literal("1"), &f), 6);
    assert_int_equal(span_get_size(f), 1);
    assert_ptr_equal(span_get_ptr(f), &data[6]);

    assert_int_equal(span_find(s, 0, span_from_str_literal("43"), &f), 3);
    assert_int_equal(span_get_size(f), 2);
    assert_ptr_equal(span_get_ptr(f), &data[3]);
    assert_memory_equal(span_get_ptr(f), "43", 2);
}

static void span_split_success(void** state)
{
    (void)state;
    uint8_t data[8] = { '1', '2', '3', '4', '3', '2', '1', '0' };

    span_t s = span_from_memory(data);
    span_t l, r;
    
    assert_int_equal(span_split(s, 1, span_from_str_literal("32"), &l, &r), 4);
    assert_int_equal(span_get_size(l), 3);
    assert_ptr_equal(span_get_ptr(l), &data[1]);
    assert_int_equal(span_get_size(r), 2);
    assert_ptr_equal(span_get_ptr(r), &data[6]);

    assert_int_equal(span_split(s, 0, span_from_str_literal("12"), &l, &r), 0);
    assert_int_equal(span_get_size(l), 0);
    assert_ptr_equal(span_get_ptr(l), NULL);
    assert_int_equal(span_get_size(r), 6);
    assert_ptr_equal(span_get_ptr(r), &data[2]);

    assert_int_equal(span_split(s, 0, span_from_str_literal("10"), &l, &r), 6);
    assert_int_equal(span_get_size(l), 6);
    assert_ptr_equal(span_get_ptr(l), &data[0]);
    assert_int_equal(span_get_size(r), 0);
    assert_ptr_equal(span_get_ptr(r), NULL);
}

static void span_split_failure(void** state)
{
    (void)state;
    uint8_t data[8] = { '1', '2', '3', '4', '3', '2', '1', '0' };

    span_t s = span_from_memory(data);
    span_t l, r;

    // token not present.    
    assert_int_equal(span_split(s, 0, span_from_str_literal("999"), &l, &r), -1);

    // Start index less than zero.
    assert_int_equal(span_split(s, -1, span_from_str_literal("32"), &l, &r), -1);

    // Start index more than size of span and target.
    assert_int_equal(span_split(s, 7, span_from_str_literal("21"), &l, &r), -1);

    // Target larger than span.
    assert_int_equal(span_split(s, 0, span_from_str_literal("1234567890"), &l, &r), -1);

    // span is NULL.
    assert_int_equal(span_split(SPAN_EMPTY, 0, span_from_str_literal("21"), &l, &r), -1);

    // Target is NULL.
    assert_int_equal(span_split(s, 0, SPAN_EMPTY, &l, &r), -1);
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
      cmocka_unit_test(span_find_success),
      cmocka_unit_test(span_split_success),
      cmocka_unit_test(span_split_failure)
  };

  return cmocka_run_group_tests_name("span_tests", tests, NULL, NULL);
}
