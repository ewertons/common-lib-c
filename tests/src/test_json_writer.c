#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include "tests.h"
#include "json.h"

static void writer_simple_object(void** state)
{
    (void)state;
    uint8_t buf[128];
    json_writer_t w;
    assert_int_equal(ok, json_writer_init(&w, span_from_memory(buf), NULL));

    assert_int_equal(ok, json_writer_append_begin_object(&w));
    assert_int_equal(ok, json_writer_append_property_name(&w, span_from_str_literal("name")));
    assert_int_equal(ok, json_writer_append_string(&w, span_from_str_literal("hi")));
    assert_int_equal(ok, json_writer_append_property_name(&w, span_from_str_literal("n")));
    assert_int_equal(ok, json_writer_append_int32(&w, -42));
    assert_int_equal(ok, json_writer_append_property_name(&w, span_from_str_literal("ok")));
    assert_int_equal(ok, json_writer_append_bool(&w, true));
    assert_int_equal(ok, json_writer_append_property_name(&w, span_from_str_literal("z")));
    assert_int_equal(ok, json_writer_append_null(&w));
    assert_int_equal(ok, json_writer_append_end_object(&w));

    span_t out = json_writer_get_bytes_written(&w);
    const char* expected = "{\"name\":\"hi\",\"n\":-42,\"ok\":true,\"z\":null}";
    assert_int_equal(strlen(expected), span_get_size(out));
    assert_memory_equal(expected, span_get_ptr(out), span_get_size(out));
}

static void writer_array_of_numbers(void** state)
{
    (void)state;
    uint8_t buf[64];
    json_writer_t w;
    assert_int_equal(ok, json_writer_init(&w, span_from_memory(buf), NULL));

    assert_int_equal(ok, json_writer_append_begin_array(&w));
    assert_int_equal(ok, json_writer_append_int32(&w, 1));
    assert_int_equal(ok, json_writer_append_int32(&w, 2));
    assert_int_equal(ok, json_writer_append_int32(&w, 3));
    assert_int_equal(ok, json_writer_append_end_array(&w));

    span_t out = json_writer_get_bytes_written(&w);
    const char* expected = "[1,2,3]";
    assert_int_equal(strlen(expected), span_get_size(out));
    assert_memory_equal(expected, span_get_ptr(out), span_get_size(out));
}

static void writer_string_escapes(void** state)
{
    (void)state;
    uint8_t buf[128];
    json_writer_t w;
    assert_int_equal(ok, json_writer_init(&w, span_from_memory(buf), NULL));

    /* Contains: backslash, quote, newline, tab, control byte 0x01 */
    uint8_t raw[] = { 'a', '\\', '"', '\n', '\t', 0x01, 'z' };
    assert_int_equal(ok, json_writer_append_string(&w, span_init(raw, sizeof(raw))));

    span_t out = json_writer_get_bytes_written(&w);
    const char* expected = "\"a\\\\\\\"\\n\\t\\u0001z\"";
    assert_int_equal(strlen(expected), span_get_size(out));
    assert_memory_equal(expected, span_get_ptr(out), span_get_size(out));
}

static void writer_double(void** state)
{
    (void)state;
    uint8_t buf[64];
    json_writer_t w;
    assert_int_equal(ok, json_writer_init(&w, span_from_memory(buf), NULL));

    assert_int_equal(ok, json_writer_append_begin_array(&w));
    assert_int_equal(ok, json_writer_append_double(&w, 3.14, 2));
    assert_int_equal(ok, json_writer_append_double(&w, -0.5, 3));
    assert_int_equal(ok, json_writer_append_double(&w, 100.0, 4));
    assert_int_equal(ok, json_writer_append_end_array(&w));

    span_t out = json_writer_get_bytes_written(&w);
    const char* expected = "[3.14,-0.5,100]";
    assert_int_equal(strlen(expected), span_get_size(out));
    assert_memory_equal(expected, span_get_ptr(out), span_get_size(out));
}

static void writer_nested(void** state)
{
    (void)state;
    uint8_t buf[256];
    json_writer_t w;
    assert_int_equal(ok, json_writer_init(&w, span_from_memory(buf), NULL));

    assert_int_equal(ok, json_writer_append_begin_object(&w));
    assert_int_equal(ok, json_writer_append_property_name(&w, span_from_str_literal("a")));
    assert_int_equal(ok, json_writer_append_begin_array(&w));
    assert_int_equal(ok, json_writer_append_int32(&w, 1));
    assert_int_equal(ok, json_writer_append_begin_object(&w));
    assert_int_equal(ok, json_writer_append_property_name(&w, span_from_str_literal("k")));
    assert_int_equal(ok, json_writer_append_string(&w, span_from_str_literal("v")));
    assert_int_equal(ok, json_writer_append_end_object(&w));
    assert_int_equal(ok, json_writer_append_end_array(&w));
    assert_int_equal(ok, json_writer_append_end_object(&w));

    span_t out = json_writer_get_bytes_written(&w);
    const char* expected = "{\"a\":[1,{\"k\":\"v\"}]}";
    assert_int_equal(strlen(expected), span_get_size(out));
    assert_memory_equal(expected, span_get_ptr(out), span_get_size(out));
}

static void writer_invalid_state_rejected(void** state)
{
    (void)state;
    uint8_t buf[64];
    json_writer_t w;

    /* property name outside object -> invalid_argument */
    assert_int_equal(ok, json_writer_init(&w, span_from_memory(buf), NULL));
    assert_int_not_equal(ok, json_writer_append_property_name(&w, span_from_str_literal("k")));

    /* end array when in object -> invalid_argument */
    assert_int_equal(ok, json_writer_init(&w, span_from_memory(buf), NULL));
    assert_int_equal(ok, json_writer_append_begin_object(&w));
    assert_int_not_equal(ok, json_writer_append_end_array(&w));

    /* two values back-to-back at top level -> invalid_argument */
    assert_int_equal(ok, json_writer_init(&w, span_from_memory(buf), NULL));
    assert_int_equal(ok, json_writer_append_int32(&w, 1));
    assert_int_not_equal(ok, json_writer_append_int32(&w, 2));
}

static void writer_insufficient_size(void** state)
{
    (void)state;
    uint8_t buf[3];
    json_writer_t w;
    assert_int_equal(ok, json_writer_init(&w, span_from_memory(buf), NULL));
    assert_int_equal(ok, json_writer_append_begin_object(&w));
    /* Need: ',' (none, first prop) + '"abc"' + ':' = 7 bytes; only 2 left -> insufficient_size */
    assert_int_equal(insufficient_size,
        json_writer_append_property_name(&w, span_from_str_literal("abc")));
}

static void writer_append_json_text_works(void** state)
{
    (void)state;
    uint8_t buf[128];
    json_writer_t w;
    assert_int_equal(ok, json_writer_init(&w, span_from_memory(buf), NULL));

    assert_int_equal(ok, json_writer_append_begin_object(&w));
    assert_int_equal(ok, json_writer_append_property_name(&w, span_from_str_literal("nested")));
    assert_int_equal(ok, json_writer_append_json_text(&w, span_from_str_literal("{\"a\":1,\"b\":[1,2]}")));
    assert_int_equal(ok, json_writer_append_property_name(&w, span_from_str_literal("k")));
    assert_int_equal(ok, json_writer_append_int32(&w, 7));
    assert_int_equal(ok, json_writer_append_end_object(&w));

    span_t out = json_writer_get_bytes_written(&w);
    const char* expected = "{\"nested\":{\"a\":1,\"b\":[1,2]},\"k\":7}";
    assert_int_equal(strlen(expected), span_get_size(out));
    assert_memory_equal(expected, span_get_ptr(out), span_get_size(out));

    /* Invalid JSON fragment is rejected. */
    assert_int_equal(ok, json_writer_init(&w, span_from_memory(buf), NULL));
    assert_int_not_equal(ok, json_writer_append_json_text(&w, span_from_str_literal("{bad")));
}

int test_json_writer()
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(writer_simple_object),
        cmocka_unit_test(writer_array_of_numbers),
        cmocka_unit_test(writer_string_escapes),
        cmocka_unit_test(writer_double),
        cmocka_unit_test(writer_nested),
        cmocka_unit_test(writer_invalid_state_rejected),
        cmocka_unit_test(writer_insufficient_size),
        cmocka_unit_test(writer_append_json_text_works),
    };
    return cmocka_run_group_tests_name("json_writer_tests", tests, NULL, NULL);
}
