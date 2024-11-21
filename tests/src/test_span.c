#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

#include "span.h"
#include "tests.h"

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
    assert_int_equal(span_get_size(f), 7);
    assert_ptr_equal(span_get_ptr(f), &data[1]);

    assert_int_equal(span_find(s, 1, span_from_str_literal("1"), &f), 6);
    assert_int_equal(span_get_size(f), 1);
    assert_ptr_equal(span_get_ptr(f), &data[7]);

    assert_int_equal(span_find(s, 0, span_from_str_literal("43"), &f), 3);
    assert_int_equal(span_get_size(f), 3);
    assert_ptr_equal(span_get_ptr(f), &data[5]);
}

static void span_split_success(void** state)
{
    (void)state;
    uint8_t data[8] = { '1', '2', '3', '4', '3', '2', '1', '0' };

    span_t s = span_from_memory(data);
    span_t l, r;
    
    assert_int_equal(span_split(s, 1, span_from_str_literal("32"), &l, &r), 0);
    assert_int_equal(span_get_size(l), 3);
    assert_ptr_equal(span_get_ptr(l), &data[1]);
    assert_int_equal(span_get_size(r), 2);
    assert_ptr_equal(span_get_ptr(r), &data[6]);

    assert_int_equal(span_split(s, 0, span_from_str_literal("12"), &l, &r), 0);
    assert_int_equal(span_get_size(l), 0);
    assert_ptr_equal(span_get_ptr(l), NULL);
    assert_int_equal(span_get_size(r), 6);
    assert_ptr_equal(span_get_ptr(r), &data[2]);

    assert_int_equal(span_split(s, 0, span_from_str_literal("10"), &l, &r), 0);
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
    assert_int_not_equal(span_split(s, 0, span_from_str_literal("999"), &l, &r), 0);

    // Start index less than zero.
    assert_int_not_equal(span_split(s, -1, span_from_str_literal("32"), &l, &r), 0);

    // Start index more than size of span and target.
    assert_int_not_equal(span_split(s, 7, span_from_str_literal("21"), &l, &r), 0);

    // Target larger than span.
    assert_int_not_equal(span_split(s, 0, span_from_str_literal("1234567890"), &l, &r), 0);

    // span is NULL.
    assert_int_not_equal(span_split(SPAN_EMPTY, 0, span_from_str_literal("21"), &l, &r), 0);

    // Target is NULL.
    assert_int_not_equal(span_split(s, 0, SPAN_EMPTY, &l, &r), 0);
}

static void span_regex_is_match_success(void** state)
{
    (void)state;
    span_t string = span_from_memory("https://www.bing.com");
    span_t pattern = span_from_memory("[w]+\\.[a-z]+\\.com");

    assert_int_equal(span_regex_is_match(string, pattern, NULL, 0, NULL), ok);
}

static void span_regex_is_match_with_matches_success(void** state)
{
    (void)state;
    span_t string = span_from_memory("https://www.bing.com");
    span_t pattern = span_from_memory("[w]+\\.[a-z]+\\.com");

    span_t matches[3];
    uint16_t number_of_matches;

    assert_int_equal(span_regex_is_match(string, pattern, matches, sizeofarray(matches), &number_of_matches), ok);
    assert_int_equal(number_of_matches, 1);
    assert_memory_equal(span_get_ptr(matches[0]), "www.bing.com", span_get_size(matches[0]));
}

static void span_regex_is_match_with_matches_subgroups_success(void** state)
{
    (void)state;
    span_t string = span_from_memory("https://www.bing.com");
    span_t pattern = span_from_memory("([w]+)\\.([a-z]+)\\.com");

    span_t matches[3];
    uint16_t number_of_matches;

    assert_int_equal(span_regex_is_match(string, pattern, matches, sizeofarray(matches), &number_of_matches), ok);
    assert_int_equal(number_of_matches, 3);
    assert_memory_equal(span_get_ptr(matches[0]), "www.bing.com", span_get_size(matches[0]));
    assert_memory_equal(span_get_ptr(matches[1]), "www", span_get_size(matches[1]));
    assert_memory_equal(span_get_ptr(matches[2]), "bing", span_get_size(matches[2]));
}

static void span_regex_is_match_empty_string_fails(void** state)
{
    (void)state;
    span_t string = SPAN_EMPTY;
    span_t pattern = span_from_memory("([w]+)\\.([a-z]+)\\.com");

    assert_int_equal(span_regex_is_match(string, pattern, NULL, 0, NULL), invalid_argument);
}

static void span_regex_is_match_empty_pattern_fails(void** state)
{
    (void)state;
    span_t string = span_from_memory("https://www.bing.com");
    span_t pattern = SPAN_EMPTY;

    assert_int_equal(span_regex_is_match(string, pattern, NULL, 0, NULL), invalid_argument);
}

static void span_regex_is_match_with_matches_NULL_matches_fails(void** state)
{
    (void)state;
    span_t string = span_from_memory("https://www.bing.com");
    span_t pattern = span_from_memory("([w]+)\\.([a-z]+)\\.com");

    uint16_t number_of_matches;

    assert_int_equal(span_regex_is_match(string, pattern, NULL, 10, &number_of_matches), invalid_argument);
}

static void span_regex_is_match_with_matches_zero_size_matches_fails(void** state)
{
    (void)state;
    span_t string = span_from_memory("https://www.bing.com");
    span_t pattern = span_from_memory("([w]+)\\.([a-z]+)\\.com");

    span_t matches[3];
    uint16_t number_of_matches;

    assert_int_equal(span_regex_is_match(string, pattern, matches, 0, &number_of_matches), invalid_argument);
}

static void span_regex_is_match_with_matches_NULL_number_of_matches_fails(void** state)
{
    (void)state;
    span_t string = span_from_memory("https://www.bing.com");
    span_t pattern = span_from_memory("([w]+)\\.([a-z]+)\\.com");

    span_t matches[3];
    uint16_t number_of_matches;

    assert_int_equal(span_regex_is_match(string, pattern, matches, sizeofarray(matches), NULL), invalid_argument);
}

static void span_regex_is_match_with_matches_matches_too_long_fails(void** state)
{
    (void)state;
    span_t string = span_from_memory("https://www.bing.com");
    span_t pattern = span_from_memory("([w]+)\\.([a-z]+)\\.com");

    span_t matches[11];
    uint16_t number_of_matches;

    assert_int_equal(span_regex_is_match(string, pattern, matches, sizeofarray(matches), &number_of_matches), invalid_argument);
}

static void span_set_success(void** state)
{
    (void)state;
    uint8_t data_raw[8] = { '1', '2', '3', '4', '3', '2', '1', '0' };

    span_t d = span_from_memory(data_raw);

    assert_int_equal(span_set(d, 0, '9'), 0);
    assert_int_equal(span_set(d, 2, '1'), 0);
    assert_int_equal(span_set(d, 4, '1'), 0);
    assert_int_equal(span_set(d, 7, '9'), 0);

    assert_memory_equal(data_raw, "92141219", 8);
}

static void span_copy_u8_success(void** state)
{
    (void)state;
    uint8_t data[8] = { 0 };

    span_t d = span_from_memory(data);

    assert_int_equal(span_copy_u8(d, 'e', &d), 0);
    assert_int_equal(span_copy_u8(d, 'd', &d), 0);
    assert_int_equal(span_copy_u8(d, 'c', &d), 0);
    assert_int_equal(span_copy_u8(d, 'b', &d), 0);
    assert_int_equal(span_copy_u8(d, 'b' - 1, &d), 0);
    assert_int_equal(span_copy_u8(d, 0, &d), 0);

    assert_memory_equal(data, "edcba\0", 6);
}

static void span_from_int32_success(void** state)
{
    (void)state;
    uint8_t data_raw[11] = { 0 };

    span_t data = span_from_memory(data_raw);
    span_t remainder;
    span_t result;

    result = span_from_int32(data, 0, &remainder);
    assert_int_equal(span_get_size(result), 1);
    assert_int_equal(span_get_size(remainder), span_get_size(data) - span_get_size(result));
    assert_memory_equal(span_get_ptr(result), "0", span_get_size(result));

    result = span_from_int32(data, 1234, &remainder);
    assert_int_equal(span_get_size(result), 4);
    assert_int_equal(span_get_size(remainder), span_get_size(data) - span_get_size(result));
    assert_memory_equal(span_get_ptr(result), "1234", span_get_size(result));

    result = span_from_int32(data, 2147483647, &remainder);
    assert_int_equal(span_get_size(result), 10);
    assert_int_equal(span_get_size(remainder), span_get_size(data) - span_get_size(result));
    assert_memory_equal(span_get_ptr(result), "2147483647", span_get_size(result));

    result = span_from_int32(data, -1234, &remainder);
    assert_int_equal(span_get_size(result), 5);
    assert_int_equal(span_get_size(remainder), span_get_size(data) - span_get_size(result));
    assert_memory_equal(span_get_ptr(result), "-1234", span_get_size(result));

    result = span_from_int32(data, -2147483648, &remainder);
    assert_int_equal(span_get_size(result), 11);
    assert_int_equal(span_get_size(remainder), span_get_size(data) - span_get_size(result));
    assert_memory_equal(span_get_ptr(result), "-2147483648", span_get_size(result));
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
      cmocka_unit_test(span_split_failure),
      cmocka_unit_test(span_regex_is_match_success),
      cmocka_unit_test(span_regex_is_match_with_matches_success),
      cmocka_unit_test(span_regex_is_match_with_matches_subgroups_success),
      cmocka_unit_test(span_regex_is_match_empty_string_fails),
      cmocka_unit_test(span_regex_is_match_empty_pattern_fails),
      cmocka_unit_test(span_regex_is_match_with_matches_NULL_matches_fails),
      cmocka_unit_test(span_regex_is_match_with_matches_zero_size_matches_fails),
      cmocka_unit_test(span_regex_is_match_with_matches_NULL_number_of_matches_fails),
      cmocka_unit_test(span_regex_is_match_with_matches_matches_too_long_fails),
      cmocka_unit_test(span_set_success),
      cmocka_unit_test(span_copy_u8_success),
      cmocka_unit_test(span_from_int32_success),
  };

  return cmocka_run_group_tests_name("span_tests", tests, NULL, NULL);
}
