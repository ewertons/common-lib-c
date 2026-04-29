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

static void base64_encode_empty_input_success(void** state)
{
  (void)state;
  uint8_t empty_raw[1] = { 0 };
  span_t input = span_init(empty_raw, 0);
  uint8_t out_raw[16];
  span_t out = span_from_memory(out_raw);

  assert_int_equal(0, base64_encode(input, out, &out));
  assert_int_equal(0, span_get_size(out));
}

static void base64_round_trip_binary_success(void** state)
{
  (void)state;
  uint8_t input_raw[] = { 0x00, 0x01, 0x02, 0xff, 0xfe, 0xfd, 0x10, 0x20, 0x30 };
  span_t input = span_from_memory(input_raw);

  uint8_t enc_raw[64];
  span_t enc = span_from_memory(enc_raw);
  assert_int_equal(0, base64_encode(input, enc, &enc));

  uint8_t dec_raw[64];
  span_t dec = span_from_memory(dec_raw);
  assert_int_equal(0, base64_decode(enc, dec, &dec));

  assert_int_equal(sizeof(input_raw), span_get_size(dec));
  assert_memory_equal(input_raw, span_get_ptr(dec), sizeof(input_raw));
}

static void base64_decode_invalid_input_fails(void** state)
{
  (void)state;
  /* Length not a multiple of 4 - definitely invalid Base64. */
  span_t bad = span_from_str_literal("abc");
  uint8_t out_raw[16];
  span_t out = span_from_memory(out_raw);

  assert_int_not_equal(0, base64_decode(bad, out, &out));
}

int test_base64()
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(base64_encode_success),
      cmocka_unit_test(base64_decode_success),
      cmocka_unit_test(base64_encode_empty_input_success),
      cmocka_unit_test(base64_round_trip_binary_success),
      cmocka_unit_test(base64_decode_invalid_input_fails),
  };

  return cmocka_run_group_tests_name("base64_tests", tests, NULL, NULL);
}
