/* SPDX-License-Identifier: MIT
 *
 * Deterministic JSON reader for common-lib-c.
 *
 * Derived from azure-sdk-for-c az_json_reader.c (Copyright (c) Microsoft
 * Corporation, MIT License). Adaptations:
 *   - span_t (uint32_t size) instead of az_span (int32_t size)
 *   - result_t instead of az_result
 *   - single contiguous buffer (no chunked / non-contiguous mode)
 *   - precondition macros replaced with explicit invalid_argument returns
 *
 * Original source:
 *   https://github.com/Azure/azure-sdk-for-c (sdk/src/azure/core/az_json_reader.c)
 */

#include <stdbool.h>
#include <stdint.h>

#include <json.h>
#include <span.h>
#include <niceties.h>

#include "json_private.h"

/* -------------------------------------------------------------------------- */
/* Tiny ASCII classifiers (avoid <ctype.h> locale dependencies).              */
/* -------------------------------------------------------------------------- */

static inline bool json_is_digit(uint8_t c)
{
    return c >= '0' && c <= '9';
}

static inline bool json_is_hex_digit(uint8_t c)
{
    return json_is_digit(c)
        || (c >= 'a' && c <= 'f')
        || (c >= 'A' && c <= 'F');
}

static inline bool json_is_whitespace(uint8_t c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static inline bool json_is_valid_escaped_char(uint8_t c)
{
    switch (c)
    {
        case '\\': case '"': case '/':
        case 'b': case 'f': case 'n': case 'r': case 't':
            return true;
        default:
            return false;
    }
}

/* -------------------------------------------------------------------------- */
/* Reader helpers                                                             */
/* -------------------------------------------------------------------------- */

static span_t reader_remaining(json_reader_t const* r)
{
    return span_slice_to_end(r->_internal.json_buffer, r->_internal.bytes_consumed);
}

/* Skip whitespace and return the resulting remaining span. */
static span_t reader_skip_whitespace(json_reader_t* r)
{
    span_t   remaining = reader_remaining(r);
    uint32_t size      = span_get_size(remaining);
    uint8_t* p         = span_get_ptr(remaining);
    uint32_t i         = 0;

    while (i < size && json_is_whitespace(p[i]))
    {
        i++;
    }
    r->_internal.bytes_consumed += i;
    return span_slice_to_end(remaining, i);
}

static void reader_set_token(json_reader_t* r, json_token_kind_t kind, span_t slice, uint32_t consumed)
{
    r->token.kind                     = kind;
    r->token.slice                    = slice;
    r->token.string_has_escaped_chars = false;
    r->current_depth                  = r->_internal.bit_stack.current_depth;

    /* Container starts: depth reported is one less than the bit stack, since
     * the stack is pushed BEFORE this update. Mirrors az_json semantics. */
    if (kind == json_token_begin_object || kind == json_token_begin_array)
    {
        if (r->current_depth > 0) r->current_depth--;
    }

    r->_internal.bytes_consumed += consumed;
}

/* -------------------------------------------------------------------------- */
/* Container start / end                                                      */
/* -------------------------------------------------------------------------- */

static result_t reader_process_container_start(json_reader_t* r, json_token_kind_t kind, json_stack_item_t item)
{
    if (r->_internal.bit_stack.current_depth >= JSON_MAX_NESTING_DEPTH)
    {
        return nesting_overflow;
    }

    span_t token = reader_remaining(r);
    json_stack_push(&r->_internal.bit_stack, item);
    reader_set_token(r, kind, span_slice(token, 0, 1), 1);
    return ok;
}

static result_t reader_process_container_end(json_reader_t* r, json_token_kind_t kind)
{
    json_stack_item_t expected =
        (kind == json_token_end_object) ? json_stack_object : json_stack_array;

    if (r->_internal.bit_stack.current_depth == 0
        || json_stack_peek(&r->_internal.bit_stack) != expected)
    {
        return unexpected_char;
    }

    span_t token = reader_remaining(r);
    (void)json_stack_pop(&r->_internal.bit_stack);
    reader_set_token(r, kind, span_slice(token, 0, 1), 1);
    return ok;
}

/* -------------------------------------------------------------------------- */
/* Strings                                                                    */
/* -------------------------------------------------------------------------- */

/* Process a string value starting at the current position which points at '"'.
 * On success, the reader's token is set to a string slice (excluding the
 * surrounding quotes), and bytes_consumed is advanced past the closing quote. */
static result_t reader_process_string(json_reader_t* r)
{
    /* Move past opening '"'. */
    r->_internal.bytes_consumed++;

    span_t   token         = reader_remaining(r);
    uint8_t* p             = span_get_ptr(token);
    uint32_t size          = span_get_size(token);
    uint32_t i             = 0;
    bool     has_escapes   = false;

    while (i < size)
    {
        uint8_t c = p[i];

        if (c == '"')
        {
            /* Found closing quote. */
            span_t slice = span_slice(token, 0, i);

            r->token.kind                     = json_token_string;
            r->token.slice                    = slice;
            r->token.string_has_escaped_chars = has_escapes;
            r->current_depth                  = r->_internal.bit_stack.current_depth;

            r->_internal.bytes_consumed += i + 1U;  /* +1 for closing quote */
            return ok;
        }

        if (c == '\\')
        {
            has_escapes = true;
            i++;
            if (i >= size) return unexpected_end;

            uint8_t esc = p[i];
            if (esc == 'u')
            {
                /* Expect 4 hex digits. */
                if (i + 4U >= size) return unexpected_end;
                for (uint32_t h = 0; h < 4; h++)
                {
                    if (!json_is_hex_digit(p[i + 1U + h])) return unexpected_char;
                }
                i += 5U;  /* 'u' + 4 hex */
                continue;
            }
            if (!json_is_valid_escaped_char(esc)) return unexpected_char;
            i++;
            continue;
        }

        if (c < JSON_ASCII_SPACE_CHARACTER)
        {
            /* Unescaped control character is invalid in JSON strings. */
            return unexpected_char;
        }
        i++;
    }

    /* Reached end without closing quote. */
    return unexpected_end;
}

static result_t reader_process_property_name(json_reader_t* r)
{
    result_t res = reader_process_string(r);
    if (res != ok) return res;

    /* Override kind set by reader_process_string. */
    r->token.kind = json_token_property_name;

    /* Skip whitespace then expect ':'. */
    span_t after = reader_skip_whitespace(r);
    if (span_get_size(after) < 1U) return unexpected_end;
    if (span_get_ptr(after)[0] != ':') return unexpected_char;
    r->_internal.bytes_consumed++;
    return ok;
}

/* -------------------------------------------------------------------------- */
/* Numbers                                                                    */
/* -------------------------------------------------------------------------- */

static bool json_is_number_delimiter(uint8_t c)
{
    return c == ',' || c == '}' || c == ']'
        || c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* Consume contiguous digits starting at index `*idx` within span `s`. */
static void reader_consume_digits(span_t s, uint32_t* idx)
{
    uint32_t size = span_get_size(s);
    uint8_t* p    = span_get_ptr(s);
    while (*idx < size && json_is_digit(p[*idx]))
    {
        (*idx)++;
    }
}

static result_t reader_process_number(json_reader_t* r)
{
    span_t   token = reader_remaining(r);
    uint8_t* p     = span_get_ptr(token);
    uint32_t size  = span_get_size(token);
    uint32_t i     = 0;

    /* Optional leading minus. */
    if (p[i] == '-')
    {
        i++;
        if (i >= size || !json_is_digit(p[i])) return unexpected_char;
    }

    /* Integer part: either a single '0' or [1-9][0-9]*. */
    if (p[i] == '0')
    {
        i++;
    }
    else
    {
        if (!json_is_digit(p[i])) return unexpected_char;
        reader_consume_digits(token, &i);
    }

    /* Optional fractional part. */
    if (i < size && p[i] == '.')
    {
        i++;
        if (i >= size || !json_is_digit(p[i])) return unexpected_char;
        reader_consume_digits(token, &i);
    }

    /* Optional exponent. */
    if (i < size && (p[i] == 'e' || p[i] == 'E'))
    {
        i++;
        if (i >= size) return unexpected_char;
        if (p[i] == '+' || p[i] == '-')
        {
            i++;
            if (i >= size) return unexpected_char;
        }
        if (!json_is_digit(p[i])) return unexpected_char;
        reader_consume_digits(token, &i);
    }

    /* What follows must be either end-of-buffer (only valid if we're at top
     * level / single value), or a structural delimiter. */
    if (i < size)
    {
        if (!json_is_number_delimiter(p[i])) return unexpected_char;
    }
    else if (r->_internal.is_complex_json)
    {
        /* Inside an object/array but ran out of bytes -> truncated. */
        return unexpected_end;
    }

    reader_set_token(r, json_token_number, span_slice(token, 0, i), i);
    return ok;
}

/* -------------------------------------------------------------------------- */
/* Literals (true / false / null)                                             */
/* -------------------------------------------------------------------------- */

static result_t reader_process_literal(json_reader_t* r, span_t literal, json_token_kind_t kind)
{
    span_t   token = reader_remaining(r);
    uint32_t lsize = span_get_size(literal);
    if (span_get_size(token) < lsize) return unexpected_end;

    if (span_compare(span_slice(token, 0, lsize), literal) != 0)
    {
        return unexpected_char;
    }

    reader_set_token(r, kind, span_slice(token, 0, lsize), lsize);
    return ok;
}

/* -------------------------------------------------------------------------- */
/* Dispatch                                                                   */
/* -------------------------------------------------------------------------- */

static result_t reader_process_value(json_reader_t* r, uint8_t first_byte)
{
    if (first_byte == '"')  return reader_process_string(r);
    if (first_byte == '{')  return reader_process_container_start(r, json_token_begin_object, json_stack_object);
    if (first_byte == '[')  return reader_process_container_start(r, json_token_begin_array,  json_stack_array);
    if (json_is_digit(first_byte) || first_byte == '-')
        return reader_process_number(r);
    if (first_byte == 't')  return reader_process_literal(r, span_from_str_literal("true"),  json_token_true);
    if (first_byte == 'f')  return reader_process_literal(r, span_from_str_literal("false"), json_token_false);
    if (first_byte == 'n')  return reader_process_literal(r, span_from_str_literal("null"),  json_token_null);
    return unexpected_char;
}

static result_t reader_read_first_token(json_reader_t* r, uint8_t first_byte)
{
    if (first_byte == '{' || first_byte == '[')
    {
        r->_internal.is_complex_json = true;
    }
    return reader_process_value(r, first_byte);
}

static result_t reader_process_after_value(json_reader_t* r, uint8_t next_byte)
{
    /* Extra data after a single top-level value is invalid. */
    if (r->_internal.bit_stack.current_depth == 0)
    {
        return unexpected_char;
    }

    bool within_object = json_stack_peek(&r->_internal.bit_stack) == json_stack_object;

    if (next_byte == ',')
    {
        r->_internal.bytes_consumed++;
        span_t after = reader_skip_whitespace(r);
        if (span_get_size(after) < 1U) return unexpected_end;
        uint8_t nb = span_get_ptr(after)[0];

        if (within_object)
        {
            if (nb != '"') return unexpected_char;
            return reader_process_property_name(r);
        }
        return reader_process_value(r, nb);
    }

    if (next_byte == '}') return reader_process_container_end(r, json_token_end_object);
    if (next_byte == ']') return reader_process_container_end(r, json_token_end_array);

    return unexpected_char;
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

result_t json_reader_init(json_reader_t* out_reader, span_t json_buffer, json_reader_options_t const* options)
{
    (void)options;
    if (out_reader == NULL || span_is_empty(json_buffer))
    {
        return invalid_argument;
    }

    *out_reader = (json_reader_t){
        .token = (json_token_t){
            .kind                     = json_token_none,
            .slice                    = SPAN_EMPTY,
            .string_has_escaped_chars = false,
        },
        .current_depth = 0,
        ._internal = {
            .json_buffer     = json_buffer,
            .bytes_consumed  = 0,
            .is_complex_json = false,
            .bit_stack       = { 0, 0 },
        },
    };
    return ok;
}

result_t json_reader_next_token(json_reader_t* reader)
{
    if (reader == NULL) return invalid_argument;

    span_t json = reader_skip_whitespace(reader);

    if (span_get_size(json) < 1U)
    {
        if (reader->token.kind == json_token_none
            || reader->_internal.bit_stack.current_depth != 0)
        {
            return unexpected_end;
        }
        return json_reader_done;
    }

    uint8_t first = span_get_ptr(json)[0];

    switch (reader->token.kind)
    {
        case json_token_none:
            return reader_read_first_token(reader, first);

        case json_token_begin_object:
            if (first == '}') return reader_process_container_end(reader, json_token_end_object);
            if (first != '"') return unexpected_char;
            return reader_process_property_name(reader);

        case json_token_begin_array:
            if (first == ']') return reader_process_container_end(reader, json_token_end_array);
            return reader_process_value(reader, first);

        case json_token_property_name:
            return reader_process_value(reader, first);

        case json_token_end_object:
        case json_token_end_array:
        case json_token_string:
        case json_token_number:
        case json_token_true:
        case json_token_false:
        case json_token_null:
            return reader_process_after_value(reader, first);

        default:
            return invalid_state;
    }
}

result_t json_reader_skip_children(json_reader_t* reader)
{
    if (reader == NULL) return invalid_argument;

    if (reader->token.kind == json_token_property_name)
    {
        result_t r = json_reader_next_token(reader);
        if (r != ok) return r;
    }

    json_token_kind_t kind = reader->token.kind;
    if (kind == json_token_begin_object || kind == json_token_begin_array)
    {
        uint32_t target_depth = reader->_internal.bit_stack.current_depth;
        do
        {
            result_t r = json_reader_next_token(reader);
            if (r != ok) return r;
        } while (reader->_internal.bit_stack.current_depth >= target_depth);
    }
    return ok;
}
