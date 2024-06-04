#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

#include "tests.h"
#include "base64.h"

#include <stdio.h>

static void base64_encode_success(void** state)
{
  (void)state;
  span_t original_data = span_from_str_literal("Hello world!");
  uint8_t encoded_data_raw[256];
  span_t encoded_data = span_from_memory(encoded_data_raw);

  assert_int_equal(0, base64_encode(original_data, encoded_data, &encoded_data));
  assert_int_equal(span_get_size(encoded_data), 16);
  assert_memory_equal(span_get_ptr(encoded_data), "SGVsbG8gd29ybGQh", span_get_size(encoded_data));
}

static void base64_decode_success(void** state)
{
  (void)state;
  span_t original_data = span_from_str_literal("SGVsbG8gd29ybGQh");
  uint8_t decoded_data_raw[256];
  span_t decoded_data = span_from_memory(decoded_data_raw);

  assert_int_equal(0, base64_decode(original_data, decoded_data, &decoded_data));

  assert_int_equal(span_get_size(decoded_data), 12);
  assert_memory_equal(span_get_ptr(decoded_data), "Hello world!", span_get_size(decoded_data));
}

int test_base64()
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(base64_encode_success),
      cmocka_unit_test(base64_decode_success),
  };

  return cmocka_run_group_tests_name("base64_tests", tests, NULL, NULL);
}
