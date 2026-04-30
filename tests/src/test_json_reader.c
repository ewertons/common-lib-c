#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include "tests.h"
#include "json.h"

static span_t S(const char* s)
{
    return span_init((uint8_t*)s, (uint32_t)strlen(s));
}

static void reader_init_rejects_empty(void** state)
{
    (void)state;
    json_reader_t r;
    assert_int_equal(invalid_argument, json_reader_init(&r, SPAN_EMPTY, NULL));
}

static void reader_top_level_primitives(void** state)
{
    (void)state;
    json_reader_t r;

    /* number */
    assert_int_equal(ok, json_reader_init(&r, S("  123  "), NULL));
    assert_int_equal(ok, json_reader_next_token(&r));
    assert_int_equal(json_token_number, r.token.kind);
    assert_int_equal(3, span_get_size(r.token.slice));
    assert_int_equal(json_reader_done, json_reader_next_token(&r));

    /* true */
    assert_int_equal(ok, json_reader_init(&r, S("true"), NULL));
    assert_int_equal(ok, json_reader_next_token(&r));
    assert_int_equal(json_token_true, r.token.kind);
    assert_int_equal(json_reader_done, json_reader_next_token(&r));

    /* null */
    assert_int_equal(ok, json_reader_init(&r, S("null"), NULL));
    assert_int_equal(ok, json_reader_next_token(&r));
    assert_int_equal(json_token_null, r.token.kind);
    assert_int_equal(json_reader_done, json_reader_next_token(&r));

    /* string */
    assert_int_equal(ok, json_reader_init(&r, S("\"hello\""), NULL));
    assert_int_equal(ok, json_reader_next_token(&r));
    assert_int_equal(json_token_string, r.token.kind);
    assert_int_equal(5, span_get_size(r.token.slice));
    assert_memory_equal("hello", span_get_ptr(r.token.slice), 5);
    assert_int_equal(json_reader_done, json_reader_next_token(&r));
}

static void reader_object_simple(void** state)
{
    (void)state;
    json_reader_t r;
    assert_int_equal(ok, json_reader_init(&r, S("{\"a\":1,\"b\":\"x\"}"), NULL));

    assert_int_equal(ok, json_reader_next_token(&r));
    assert_int_equal(json_token_begin_object, r.token.kind);

    assert_int_equal(ok, json_reader_next_token(&r));
    assert_int_equal(json_token_property_name, r.token.kind);
    assert_int_equal(0, span_compare(r.token.slice, span_from_str_literal("a")));

    assert_int_equal(ok, json_reader_next_token(&r));
    assert_int_equal(json_token_number, r.token.kind);

    assert_int_equal(ok, json_reader_next_token(&r));
    assert_int_equal(json_token_property_name, r.token.kind);
    assert_int_equal(0, span_compare(r.token.slice, span_from_str_literal("b")));

    assert_int_equal(ok, json_reader_next_token(&r));
    assert_int_equal(json_token_string, r.token.kind);

    assert_int_equal(ok, json_reader_next_token(&r));
    assert_int_equal(json_token_end_object, r.token.kind);

    assert_int_equal(json_reader_done, json_reader_next_token(&r));
}

static void reader_array_nested(void** state)
{
    (void)state;
    json_reader_t r;
    assert_int_equal(ok, json_reader_init(&r, S("[1,[2,3],{\"k\":4}]"), NULL));

    json_token_kind_t expected[] = {
        json_token_begin_array,
        json_token_number,        /* 1 */
        json_token_begin_array,
        json_token_number,        /* 2 */
        json_token_number,        /* 3 */
        json_token_end_array,
        json_token_begin_object,
        json_token_property_name, /* k */
        json_token_number,        /* 4 */
        json_token_end_object,
        json_token_end_array
    };
    for (size_t i = 0; i < sizeof(expected)/sizeof(expected[0]); i++)
    {
        assert_int_equal(ok, json_reader_next_token(&r));
        assert_int_equal(expected[i], r.token.kind);
    }
    assert_int_equal(json_reader_done, json_reader_next_token(&r));
}

static void reader_string_escapes_marked(void** state)
{
    (void)state;
    json_reader_t r;
    assert_int_equal(ok, json_reader_init(&r, S("\"a\\nb\""), NULL));
    assert_int_equal(ok, json_reader_next_token(&r));
    assert_int_equal(json_token_string, r.token.kind);
    assert_true(r.token.string_has_escaped_chars);
    /* slice contains 4 chars: a \\ n b   (the raw escape sequence). */
    assert_int_equal(4, span_get_size(r.token.slice));

    /* \uXXXX accepted */
    assert_int_equal(ok, json_reader_init(&r, S("\"x\\u00ABy\""), NULL));
    assert_int_equal(ok, json_reader_next_token(&r));
    assert_int_equal(json_token_string, r.token.kind);
    assert_true(r.token.string_has_escaped_chars);
}

static void reader_invalid_inputs(void** state)
{
    (void)state;
    json_reader_t r;

    /* unterminated string */
    assert_int_equal(ok, json_reader_init(&r, S("\"oops"), NULL));
    assert_int_equal(unexpected_end, json_reader_next_token(&r));

    /* invalid escape */
    assert_int_equal(ok, json_reader_init(&r, S("\"\\q\""), NULL));
    assert_int_equal(unexpected_char, json_reader_next_token(&r));

    /* bare control char */
    char ctrl[] = { '"', 0x01, '"', 0 };
    assert_int_equal(ok, json_reader_init(&r, S(ctrl), NULL));
    assert_int_equal(unexpected_char, json_reader_next_token(&r));

    /* number leading zero followed by digit -> az treats "01" as invalid:
     * "0" is parsed, then '1' is not a delimiter -> unexpected_char */
    assert_int_equal(ok, json_reader_init(&r, S("01"), NULL));
    assert_int_equal(unexpected_char, json_reader_next_token(&r));

    /* trailing comma in object */
    assert_int_equal(ok, json_reader_init(&r, S("{\"a\":1,}"), NULL));
    assert_int_equal(ok, json_reader_next_token(&r)); /* { */
    assert_int_equal(ok, json_reader_next_token(&r)); /* "a" */
    assert_int_equal(ok, json_reader_next_token(&r)); /* 1  */
    assert_int_equal(unexpected_char, json_reader_next_token(&r));

    /* mismatched container */
    assert_int_equal(ok, json_reader_init(&r, S("[}"), NULL));
    assert_int_equal(ok, json_reader_next_token(&r));
    assert_int_equal(unexpected_char, json_reader_next_token(&r));
}

static void reader_skip_children(void** state)
{
    (void)state;
    json_reader_t r;
    assert_int_equal(ok, json_reader_init(&r, S("{\"a\":{\"x\":1,\"y\":[1,2,3]},\"b\":42}"), NULL));

    assert_int_equal(ok, json_reader_next_token(&r));    /* { */
    assert_int_equal(ok, json_reader_next_token(&r));    /* "a" */
    assert_int_equal(json_token_property_name, r.token.kind);

    /* skip the entire value of "a" */
    assert_int_equal(ok, json_reader_skip_children(&r));
    assert_int_equal(json_token_end_object, r.token.kind);

    assert_int_equal(ok, json_reader_next_token(&r));    /* "b" */
    assert_int_equal(json_token_property_name, r.token.kind);
    assert_int_equal(0, span_compare(r.token.slice, span_from_str_literal("b")));

    assert_int_equal(ok, json_reader_next_token(&r));    /* 42 */
    assert_int_equal(ok, json_reader_next_token(&r));    /* } */
    assert_int_equal(json_token_end_object, r.token.kind);
    assert_int_equal(json_reader_done, json_reader_next_token(&r));
}

static void reader_writer_round_trip(void** state)
{
    (void)state;
    /* Write a small document, then read it back with the reader. */
    uint8_t buf[128];
    json_writer_t w;
    assert_int_equal(ok, json_writer_init(&w, span_from_memory(buf), NULL));
    assert_int_equal(ok, json_writer_append_begin_object(&w));
    assert_int_equal(ok, json_writer_append_property_name(&w, span_from_str_literal("hi")));
    assert_int_equal(ok, json_writer_append_string(&w, span_from_str_literal("there")));
    assert_int_equal(ok, json_writer_append_property_name(&w, span_from_str_literal("n")));
    assert_int_equal(ok, json_writer_append_int32(&w, -7));
    assert_int_equal(ok, json_writer_append_end_object(&w));

    span_t doc = json_writer_get_bytes_written(&w);
    json_reader_t r;
    assert_int_equal(ok, json_reader_init(&r, doc, NULL));

    json_token_kind_t expected[] = {
        json_token_begin_object,
        json_token_property_name,
        json_token_string,
        json_token_property_name,
        json_token_number,
        json_token_end_object
    };
    for (size_t i = 0; i < sizeof(expected)/sizeof(expected[0]); i++)
    {
        assert_int_equal(ok, json_reader_next_token(&r));
        assert_int_equal(expected[i], r.token.kind);
    }
    assert_int_equal(json_reader_done, json_reader_next_token(&r));
}

int test_json_reader()
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(reader_init_rejects_empty),
        cmocka_unit_test(reader_top_level_primitives),
        cmocka_unit_test(reader_object_simple),
        cmocka_unit_test(reader_array_nested),
        cmocka_unit_test(reader_string_escapes_marked),
        cmocka_unit_test(reader_invalid_inputs),
        cmocka_unit_test(reader_skip_children),
        cmocka_unit_test(reader_writer_round_trip),
    };
    return cmocka_run_group_tests_name("json_reader_tests", tests, NULL, NULL);
}
