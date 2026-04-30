#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <setjmp.h>
#include <string.h>
#include <math.h>
#include <cmocka.h>

#include "tests.h"
#include "json.h"

static span_t S(const char* s) { return span_init((uint8_t*)s, (uint32_t)strlen(s)); }

/* Read the first token of `text` into `out_token`. */
static void first_token(const char* text, json_token_t* out_token)
{
    json_reader_t r;
    assert_int_equal(ok, json_reader_init(&r, S(text), NULL));
    assert_int_equal(ok, json_reader_next_token(&r));
    *out_token = r.token;
}

static void token_get_boolean_works(void** state)
{
    (void)state;
    json_token_t t;
    bool b;

    first_token("true", &t);
    assert_int_equal(ok, json_token_get_boolean(&t, &b));
    assert_true(b);

    first_token("false", &t);
    assert_int_equal(ok, json_token_get_boolean(&t, &b));
    assert_false(b);

    first_token("null", &t);
    assert_int_equal(invalid_state, json_token_get_boolean(&t, &b));
}

static void token_get_int32_works(void** state)
{
    (void)state;
    json_token_t t;
    int32_t v;

    first_token("0", &t);          assert_int_equal(ok, json_token_get_int32(&t, &v)); assert_int_equal(0, v);
    first_token("123", &t);        assert_int_equal(ok, json_token_get_int32(&t, &v)); assert_int_equal(123, v);
    first_token("-456", &t);       assert_int_equal(ok, json_token_get_int32(&t, &v)); assert_int_equal(-456, v);
    first_token("2147483647", &t); assert_int_equal(ok, json_token_get_int32(&t, &v)); assert_int_equal(INT32_MAX, v);
    first_token("-2147483648", &t);assert_int_equal(ok, json_token_get_int32(&t, &v)); assert_int_equal(INT32_MIN, v);

    /* Overflow */
    first_token("2147483648", &t); assert_int_equal(unexpected_char, json_token_get_int32(&t, &v));
    /* Fractional rejected by integer getter */
    first_token("1.5", &t);        assert_int_equal(unexpected_char, json_token_get_int32(&t, &v));
    /* Wrong kind */
    first_token("true", &t);       assert_int_equal(invalid_state, json_token_get_int32(&t, &v));
}

static void token_get_uint64_works(void** state)
{
    (void)state;
    json_token_t t;
    uint64_t v;

    first_token("0", &t);                      assert_int_equal(ok, json_token_get_uint64(&t, &v)); assert_int_equal(0, (int)v);
    first_token("18446744073709551615", &t);   assert_int_equal(ok, json_token_get_uint64(&t, &v)); assert_true(v == UINT64_MAX);
    first_token("18446744073709551616", &t);   assert_int_equal(unexpected_char, json_token_get_uint64(&t, &v));
    first_token("-1", &t);                     assert_int_equal(unexpected_char, json_token_get_uint64(&t, &v));
}

static void token_get_double_works(void** state)
{
    (void)state;
    json_token_t t;
    double v;

    first_token("3.14", &t);   assert_int_equal(ok, json_token_get_double(&t, &v)); assert_true(fabs(v - 3.14) < 1e-9);
    first_token("-0.5", &t);   assert_int_equal(ok, json_token_get_double(&t, &v)); assert_true(fabs(v + 0.5) < 1e-9);
    first_token("1e3", &t);    assert_int_equal(ok, json_token_get_double(&t, &v)); assert_true(fabs(v - 1000.0) < 1e-9);
    first_token("2.5e-2", &t); assert_int_equal(ok, json_token_get_double(&t, &v)); assert_true(fabs(v - 0.025) < 1e-9);
    first_token("0", &t);      assert_int_equal(ok, json_token_get_double(&t, &v)); assert_true(v == 0.0);
}

static void token_get_string_unescapes(void** state)
{
    (void)state;
    json_token_t t;
    uint8_t buf[64];
    span_t   dst = span_from_memory(buf);
    uint32_t len = 0;

    /* No escapes -> fast path */
    first_token("\"plain\"", &t);
    assert_int_equal(ok, json_token_get_string(&t, dst, &len));
    assert_int_equal(5, len);
    assert_memory_equal("plain", buf, 5);

    /* Standard escapes */
    first_token("\"a\\nb\\tc\\\\d\\\"e\"", &t);
    assert_int_equal(ok, json_token_get_string(&t, dst, &len));
    assert_int_equal(9, len);
    assert_memory_equal("a\nb\tc\\d\"e", buf, 9);

    /* \uXXXX BMP code point -> UTF-8 (U+00E9 = 'é' = 0xC3 0xA9) */
    first_token("\"\\u00E9\"", &t);
    assert_int_equal(ok, json_token_get_string(&t, dst, &len));
    assert_int_equal(2, len);
    assert_int_equal(0xC3, buf[0]);
    assert_int_equal(0xA9, buf[1]);

    /* Surrogate pair: U+1F600 = 😀 = F0 9F 98 80 */
    first_token("\"\\uD83D\\uDE00\"", &t);
    assert_int_equal(ok, json_token_get_string(&t, dst, &len));
    assert_int_equal(4, len);
    assert_int_equal(0xF0, buf[0]);
    assert_int_equal(0x9F, buf[1]);
    assert_int_equal(0x98, buf[2]);
    assert_int_equal(0x80, buf[3]);

    /* Insufficient destination */
    uint8_t small[2];
    span_t  sdst = span_from_memory(small);
    first_token("\"hello\"", &t);
    assert_int_equal(insufficient_size, json_token_get_string(&t, sdst, &len));
}

static void token_is_text_equal_works(void** state)
{
    (void)state;
    json_token_t t;

    first_token("\"plain\"", &t);
    assert_true (json_token_is_text_equal(&t, span_from_str_literal("plain")));
    assert_false(json_token_is_text_equal(&t, span_from_str_literal("Plain")));
    assert_false(json_token_is_text_equal(&t, span_from_str_literal("plainx")));

    /* Escaped form should compare equal to the unescaped expected text. */
    first_token("\"a\\nb\"", &t);
    uint8_t expected[] = { 'a', '\n', 'b' };
    assert_true(json_token_is_text_equal(&t, span_init(expected, sizeof(expected))));
    assert_false(json_token_is_text_equal(&t, span_from_str_literal("a\\nb")));

    /* Non-string token kind */
    first_token("123", &t);
    assert_false(json_token_is_text_equal(&t, span_from_str_literal("123")));
}

static void json_string_unescape_standalone(void** state)
{
    (void)state;
    uint8_t buf[32];
    span_t  dst = span_from_memory(buf);

    span_t out = json_string_unescape(span_from_str_literal("hi\\tworld"), dst);
    assert_int_equal(8, span_get_size(out));
    assert_memory_equal("hi\tworld", span_get_ptr(out), 8);

    /* Malformed -> SPAN_EMPTY */
    out = json_string_unescape(span_from_str_literal("bad\\q"), dst);
    assert_true(span_is_empty(out));
}

int test_json_token()
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(token_get_boolean_works),
        cmocka_unit_test(token_get_int32_works),
        cmocka_unit_test(token_get_uint64_works),
        cmocka_unit_test(token_get_double_works),
        cmocka_unit_test(token_get_string_unescapes),
        cmocka_unit_test(token_is_text_equal_works),
        cmocka_unit_test(json_string_unescape_standalone),
    };
    return cmocka_run_group_tests_name("json_token_tests", tests, NULL, NULL);
}
