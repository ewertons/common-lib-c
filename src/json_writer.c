/* SPDX-License-Identifier: MIT
 *
 * Deterministic JSON writer for common-lib-c.
 *
 * Derived from azure-sdk-for-c az_json_writer.c (Copyright (c) Microsoft
 * Corporation, MIT License). Adaptations:
 *   - span_t (uint32_t size) instead of az_span (int32_t size)
 *   - result_t instead of az_result
 *   - single contiguous buffer (no chunked / non-contiguous allocator)
 *   - precondition macros replaced with explicit invalid_argument returns
 *
 * Original source:
 *   https://github.com/Azure/azure-sdk-for-c (sdk/src/azure/core/az_json_writer.c)
 */

#include <math.h>
#include <string.h>

#include <json.h>
#include <span.h>
#include <niceties.h>

#include "json_private.h"

/* -------------------------------------------------------------------------- */
/* Internal numeric formatting helpers                                        */
/* -------------------------------------------------------------------------- */

/* Format a uint64 in decimal into 'to'. Returns the number of bytes written or
 * 0 if 'to' is too small. */
static uint32_t json_format_uint64(span_t to, uint64_t value)
{
    /* Maximum decimal digits for uint64 = 20. */
    uint8_t  tmp[20];
    uint32_t n = 0;

    if (value == 0)
    {
        tmp[n++] = '0';
    }
    else
    {
        while (value != 0)
        {
            tmp[n++] = (uint8_t)('0' + (value % 10U));
            value /= 10U;
        }
    }

    if (span_get_size(to) < n)
    {
        return 0;
    }

    uint8_t* dst = span_get_ptr(to);
    for (uint32_t i = 0; i < n; i++)
    {
        dst[i] = tmp[n - 1U - i];
    }
    return n;
}

/* Format a double into 'to' using fixed notation with up to fractional_digits
 * after the decimal point, trailing zeros trimmed. Rejects NaN/Inf and
 * integer parts that cannot be represented exactly in uint64. */
static result_t json_format_double(span_t to, double value, uint32_t fractional_digits, uint32_t* out_written)
{
    if (!isfinite(value))
    {
        return invalid_argument;
    }

    if (fractional_digits > JSON_MAX_SUPPORTED_FRACTIONAL_DIGITS)
    {
        fractional_digits = JSON_MAX_SUPPORTED_FRACTIONAL_DIGITS;
    }

    span_t   remaining = to;
    uint32_t total     = 0;

    if (value < 0.0)
    {
        if (span_copy_u8(remaining, '-', &remaining) != 0)
        {
            return insufficient_size;
        }
        total++;
        value = -value;
    }

    /* Integer part. Reject values whose integer component cannot be exactly
     * represented in uint64 (mirrors az: max safe integer = 2^53 - 1, but we
     * cap at uint64 range to keep the implementation simple). */
    if (value >= 1.8446744073709552e19) /* > UINT64_MAX */
    {
        return invalid_argument;
    }

    uint64_t int_part  = (uint64_t)value;
    double   frac_part = value - (double)int_part;

    uint32_t n = json_format_uint64(remaining, int_part);
    if (n == 0)
    {
        return insufficient_size;
    }
    remaining = span_slice_to_end(remaining, n);
    total    += n;

    if (fractional_digits == 0 || frac_part == 0.0)
    {
        if (out_written != NULL) *out_written = total;
        return ok;
    }

    /* Scale the fractional part by 10^fractional_digits and round. */
    double scale = 1.0;
    for (uint32_t i = 0; i < fractional_digits; i++)
    {
        scale *= 10.0;
    }
    uint64_t frac_scaled = (uint64_t)(frac_part * scale + 0.5);

    /* Carry into the integer part if rounding pushed us over. */
    if (frac_scaled >= (uint64_t)scale)
    {
        /* Re-emit integer part incremented by 1. We need to rewrite the
         * already-written integer digits. Simplest approach: bail with
         * invalid_state to keep determinism rather than truncate badly. */
        int_part++;
        frac_scaled = 0;
        /* Rewind: rewrite just the integer (the sign byte, if any, stays). */
        remaining = span_slice_to_end(to, value < 0.0 ? 1U : 0U);
        n = json_format_uint64(remaining, int_part);
        if (n == 0)
        {
            return insufficient_size;
        }
        remaining = span_slice_to_end(remaining, n);
        total     = (value < 0.0 ? 1U : 0U) + n;
    }

    if (frac_scaled == 0)
    {
        if (out_written != NULL) *out_written = total;
        return ok;
    }

    /* Write the decimal point. */
    if (span_copy_u8(remaining, '.', &remaining) != 0)
    {
        return insufficient_size;
    }
    total++;

    /* Format fractional part as fixed-width with leading zeros, then trim
     * trailing zeros. */
    uint8_t  digits[JSON_MAX_SUPPORTED_FRACTIONAL_DIGITS];
    for (uint32_t i = 0; i < fractional_digits; i++)
    {
        digits[fractional_digits - 1U - i] = (uint8_t)('0' + (frac_scaled % 10U));
        frac_scaled /= 10U;
    }

    /* Find last non-zero digit. */
    uint32_t last = fractional_digits;
    while (last > 0 && digits[last - 1U] == '0')
    {
        last--;
    }

    if (span_get_size(remaining) < last)
    {
        return insufficient_size;
    }
    (void)memcpy(span_get_ptr(remaining), digits, last);
    total += last;

    if (out_written != NULL) *out_written = total;
    return ok;
}

/* -------------------------------------------------------------------------- */
/* Internal writer helpers                                                    */
/* -------------------------------------------------------------------------- */

static span_t writer_remaining(json_writer_t* w)
{
    return span_slice_to_end(w->_internal.destination, w->_internal.bytes_written);
}

static void writer_advance(json_writer_t* w, uint32_t bytes, bool need_comma, json_token_kind_t kind)
{
    w->_internal.bytes_written  += bytes;
    w->total_bytes_written      += bytes;
    w->_internal.need_comma      = need_comma;
    w->_internal.token_kind      = kind;
}

/* Validates that a JSON value (or container start) may be appended at the
 * current writer state. */
static bool writer_can_append_value(json_writer_t const* w)
{
    json_token_kind_t kind = w->_internal.token_kind;

    if (json_stack_peek(&w->_internal.bit_stack) == json_stack_object &&
        w->_internal.bit_stack.current_depth > 0)
    {
        /* Inside an object, only a property name may precede a value. */
        return kind == json_token_property_name;
    }

    /* Outside any container or inside an array. Multiple top-level values are
     * not allowed; a closed top-level container also blocks further appends. */
    if (w->_internal.bit_stack.current_depth == 0 && kind != json_token_none)
    {
        return false;
    }
    return true;
}

static bool writer_can_append_property_name(json_writer_t const* w)
{
    json_token_kind_t kind = w->_internal.token_kind;

    if (w->_internal.bit_stack.current_depth == 0 ||
        json_stack_peek(&w->_internal.bit_stack) != json_stack_object)
    {
        return false;
    }
    /* Two property names back-to-back is invalid. */
    return kind != json_token_property_name;
}

static bool writer_can_append_container_end(json_writer_t const* w, uint8_t byte)
{
    json_token_kind_t kind = w->_internal.token_kind;

    if (w->_internal.bit_stack.current_depth == 0 || kind == json_token_property_name)
    {
        return false;
    }

    json_stack_item_t top = json_stack_peek(&w->_internal.bit_stack);
    if (byte == ']')
    {
        return top == json_stack_array;
    }
    /* byte == '}' */
    return top == json_stack_object;
}

/* -------------------------------------------------------------------------- */
/* String escaping                                                            */
/* -------------------------------------------------------------------------- */

/* Returns the size required to write the escaped form of 'value'. Sets
 * *out_first_escape to the index of the first byte that needs escaping, or
 * (uint32_t)-1 if no byte needs escaping. */
static uint32_t json_escaped_length(span_t value, uint32_t* out_first_escape)
{
    uint32_t escaped_length = 0;
    uint32_t first_escape   = (uint32_t)-1;
    uint32_t size           = span_get_size(value);
    uint8_t* p              = span_get_ptr(value);

    for (uint32_t i = 0; i < size; i++)
    {
        uint8_t c = p[i];
        switch (c)
        {
            case '\\': case '"':
            case '\b': case '\f': case '\n': case '\r': case '\t':
                escaped_length += 2;
                break;
            default:
                if (c < JSON_ASCII_SPACE_CHARACTER)
                {
                    escaped_length += JSON_MAX_EXPANSION_FACTOR_WHILE_ESCAPING;
                }
                else
                {
                    escaped_length += 1;
                }
                break;
        }

        if (escaped_length != (i + 1U) && first_escape == (uint32_t)-1)
        {
            first_escape = i;
        }
    }

    *out_first_escape = first_escape;
    return escaped_length;
}

/* Writes one byte of 'src' into 'dst', escaping if necessary. Returns the
 * number of destination bytes consumed, or 0 on insufficient space. */
static uint32_t json_escape_one_byte(span_t* dst, uint8_t c)
{
    uint8_t escaped = 0;
    switch (c)
    {
        case '\\': case '"': escaped = c;    break;
        case '\b':           escaped = 'b';  break;
        case '\f':           escaped = 'f';  break;
        case '\n':           escaped = 'n';  break;
        case '\r':           escaped = 'r';  break;
        case '\t':           escaped = 't';  break;
        default:
            if (c < JSON_ASCII_SPACE_CHARACTER)
            {
                /* \u00XX */
                if (span_get_size(*dst) < JSON_MAX_EXPANSION_FACTOR_WHILE_ESCAPING)
                {
                    return 0;
                }
                uint8_t* p = span_get_ptr(*dst);
                p[0] = '\\';
                p[1] = 'u';
                p[2] = '0';
                p[3] = '0';
                p[4] = json_number_to_upper_hex((uint8_t)(c / JSON_NUMBER_OF_HEX_VALUES));
                p[5] = json_number_to_upper_hex((uint8_t)(c % JSON_NUMBER_OF_HEX_VALUES));
                *dst = span_slice_to_end(*dst, JSON_MAX_EXPANSION_FACTOR_WHILE_ESCAPING);
                return JSON_MAX_EXPANSION_FACTOR_WHILE_ESCAPING;
            }
            /* Unescaped passthrough. */
            if (span_copy_u8(*dst, c, dst) != 0) return 0;
            return 1;
    }

    /* Two-byte escape (\X) */
    if (span_get_size(*dst) < 2U)
    {
        return 0;
    }
    uint8_t* p = span_get_ptr(*dst);
    p[0] = '\\';
    p[1] = escaped;
    *dst = span_slice_to_end(*dst, 2);
    return 2;
}

/* -------------------------------------------------------------------------- */
/* Public writer API                                                          */
/* -------------------------------------------------------------------------- */

result_t json_writer_init(json_writer_t* out_writer, span_t destination, json_writer_options_t const* options)
{
    (void)options;
    if (out_writer == NULL)
    {
        return invalid_argument;
    }
    *out_writer = (json_writer_t){
        .total_bytes_written = 0,
        ._internal = {
            .destination   = destination,
            .bytes_written = 0,
            .need_comma    = false,
            .token_kind    = json_token_none,
            .bit_stack     = { 0, 0 }
        }
    };
    return ok;
}

/* Writes a JSON-quoted, escaped string starting at the writer's current
 * position. If 'is_property_name' is true, also emits the trailing ':'. */
static result_t writer_write_quoted_string(json_writer_t* w, span_t value, bool is_property_name)
{
    if (span_get_size(value) > JSON_MAX_UNESCAPED_STRING_SIZE)
    {
        return invalid_argument;
    }

    uint32_t first_escape   = (uint32_t)-1;
    uint32_t escaped_length = json_escaped_length(value, &first_escape);

    /* required = comma? + '"' + escaped + '"' + (is_property_name ? ':' : 0) */
    uint32_t required = escaped_length + 2U;
    if (w->_internal.need_comma)   required++;
    if (is_property_name)          required++;

    span_t remaining = writer_remaining(w);
    if (span_get_size(remaining) < required)
    {
        return insufficient_size;
    }

    if (w->_internal.need_comma)
    {
        if (span_copy_u8(remaining, ',', &remaining) != 0) return insufficient_size;
    }
    if (span_copy_u8(remaining, '"', &remaining) != 0) return insufficient_size;

    if (first_escape == (uint32_t)-1)
    {
        /* Fast path: no escaping needed. */
        if (span_get_size(value) > 0)
        {
            (void)span_copy(remaining, value, &remaining);
        }
    }
    else
    {
        /* Bulk-copy the unescaped prefix, then escape byte-by-byte. */
        if (first_escape > 0)
        {
            (void)span_copy(remaining, span_slice(value, 0, first_escape), &remaining);
        }
        uint8_t* p    = span_get_ptr(value);
        uint32_t size = span_get_size(value);
        for (uint32_t i = first_escape; i < size; i++)
        {
            if (json_escape_one_byte(&remaining, p[i]) == 0)
            {
                return insufficient_size;
            }
        }
    }

    if (span_copy_u8(remaining, '"', &remaining) != 0) return insufficient_size;
    if (is_property_name)
    {
        if (span_copy_u8(remaining, ':', &remaining) != 0) return insufficient_size;
    }

    writer_advance(w, required,
        is_property_name ? false : true,
        is_property_name ? json_token_property_name : json_token_string);
    return ok;
}

result_t json_writer_append_string(json_writer_t* writer, span_t value)
{
    if (writer == NULL || !writer_can_append_value(writer))
    {
        return invalid_argument;
    }
    return writer_write_quoted_string(writer, value, false);
}

result_t json_writer_append_property_name(json_writer_t* writer, span_t name)
{
    if (writer == NULL || span_is_empty(name) || !writer_can_append_property_name(writer))
    {
        return invalid_argument;
    }
    return writer_write_quoted_string(writer, name, true);
}

static result_t writer_append_literal(json_writer_t* w, span_t literal, json_token_kind_t kind)
{
    if (w == NULL || !writer_can_append_value(w))
    {
        return invalid_argument;
    }

    uint32_t required = span_get_size(literal);
    if (w->_internal.need_comma) required++;

    span_t remaining = writer_remaining(w);
    if (span_get_size(remaining) < required)
    {
        return insufficient_size;
    }

    if (w->_internal.need_comma)
    {
        if (span_copy_u8(remaining, ',', &remaining) != 0) return insufficient_size;
    }
    (void)span_copy(remaining, literal, NULL);

    writer_advance(w, required, true, kind);
    return ok;
}

result_t json_writer_append_bool(json_writer_t* writer, bool value)
{
    if (value)
    {
        return writer_append_literal(writer, span_from_str_literal("true"), json_token_true);
    }
    return writer_append_literal(writer, span_from_str_literal("false"), json_token_false);
}

result_t json_writer_append_null(json_writer_t* writer)
{
    return writer_append_literal(writer, span_from_str_literal("null"), json_token_null);
}

result_t json_writer_append_int32(json_writer_t* writer, int32_t value)
{
    if (writer == NULL || !writer_can_append_value(writer))
    {
        return invalid_argument;
    }

    uint32_t required = JSON_MAX_SIZE_FOR_INT32;
    if (writer->_internal.need_comma) required++;

    span_t remaining = writer_remaining(writer);
    if (span_get_size(remaining) < required)
    {
        return insufficient_size;
    }

    if (writer->_internal.need_comma)
    {
        if (span_copy_u8(remaining, ',', &remaining) != 0) return insufficient_size;
    }

    span_t leftover;
    span_t written_span = span_copy_int32(remaining, value, &leftover);
    if (span_is_empty(written_span))
    {
        return insufficient_size;
    }

    /* Bytes actually written = comma? + size of formatted number. */
    uint32_t written = (writer->_internal.need_comma ? 1U : 0U) + span_get_size(written_span);
    writer_advance(writer, written, true, json_token_number);
    return ok;
}

result_t json_writer_append_double(json_writer_t* writer, double value, uint32_t fractional_digits)
{
    if (writer == NULL || !writer_can_append_value(writer))
    {
        return invalid_argument;
    }

    uint32_t required = JSON_MAX_SIZE_FOR_WRITING_DOUBLE;
    if (writer->_internal.need_comma) required++;

    span_t remaining = writer_remaining(writer);
    if (span_get_size(remaining) < required)
    {
        return insufficient_size;
    }

    if (writer->_internal.need_comma)
    {
        if (span_copy_u8(remaining, ',', &remaining) != 0) return insufficient_size;
    }

    uint32_t formatted = 0;
    result_t r = json_format_double(remaining, value, fractional_digits, &formatted);
    if (r != ok)
    {
        return r;
    }

    uint32_t written = (writer->_internal.need_comma ? 1U : 0U) + formatted;
    writer_advance(writer, written, true, json_token_number);
    return ok;
}

static result_t writer_append_container_start(json_writer_t* w, uint8_t byte, json_token_kind_t kind)
{
    if (w == NULL || !writer_can_append_value(w))
    {
        return invalid_argument;
    }
    if (w->_internal.bit_stack.current_depth >= JSON_MAX_NESTING_DEPTH)
    {
        return nesting_overflow;
    }

    uint32_t required = 1;
    if (w->_internal.need_comma) required++;

    span_t remaining = writer_remaining(w);
    if (span_get_size(remaining) < required)
    {
        return insufficient_size;
    }

    if (w->_internal.need_comma)
    {
        if (span_copy_u8(remaining, ',', &remaining) != 0) return insufficient_size;
    }
    if (span_copy_u8(remaining, byte, &remaining) != 0) return insufficient_size;

    writer_advance(w, required, false, kind);
    json_stack_push(&w->_internal.bit_stack,
        kind == json_token_begin_object ? json_stack_object : json_stack_array);
    return ok;
}

static result_t writer_append_container_end(json_writer_t* w, uint8_t byte, json_token_kind_t kind)
{
    if (w == NULL || !writer_can_append_container_end(w, byte))
    {
        return invalid_argument;
    }

    span_t remaining = writer_remaining(w);
    if (span_get_size(remaining) < 1U)
    {
        return insufficient_size;
    }
    if (span_copy_u8(remaining, byte, &remaining) != 0) return insufficient_size;

    writer_advance(w, 1, true, kind);
    (void)json_stack_pop(&w->_internal.bit_stack);
    return ok;
}

result_t json_writer_append_begin_object(json_writer_t* writer)
{
    return writer_append_container_start(writer, '{', json_token_begin_object);
}

result_t json_writer_append_begin_array(json_writer_t* writer)
{
    return writer_append_container_start(writer, '[', json_token_begin_array);
}

result_t json_writer_append_end_object(json_writer_t* writer)
{
    return writer_append_container_end(writer, '}', json_token_end_object);
}

result_t json_writer_append_end_array(json_writer_t* writer)
{
    return writer_append_container_end(writer, ']', json_token_end_array);
}

/* Validate that json_text is a single, well-formed JSON value (object,
 * array, or primitive). Sets *first_kind / *last_kind on success. */
static result_t writer_validate_json_text(span_t json_text, json_token_kind_t* first_kind, json_token_kind_t* last_kind)
{
    json_reader_t reader;
    result_t r = json_reader_init(&reader, json_text, NULL);
    if (r != ok) return r;

    r = json_reader_next_token(&reader);
    if (r != ok) return r;
    *first_kind = reader.token.kind;

    do
    {
        r = json_reader_next_token(&reader);
    } while (r == ok);

    if (r != json_reader_done) return r;

    *last_kind = reader.token.kind;
    return ok;
}

result_t json_writer_append_json_text(json_writer_t* writer, span_t json_text)
{
    if (writer == NULL || span_is_empty(json_text))
    {
        return invalid_argument;
    }

    json_token_kind_t first_kind = json_token_none;
    json_token_kind_t last_kind  = json_token_none;
    result_t r = writer_validate_json_text(json_text, &first_kind, &last_kind);
    if (r != ok) return r;

    /* The first token of a valid JSON value is never property-name or end-*. */
    if (!writer_can_append_value(writer))
    {
        return invalid_state;
    }

    uint32_t required = span_get_size(json_text);
    if (writer->_internal.need_comma) required++;

    span_t remaining = writer_remaining(writer);
    if (span_get_size(remaining) < required)
    {
        return insufficient_size;
    }

    if (writer->_internal.need_comma)
    {
        if (span_copy_u8(remaining, ',', &remaining) != 0) return insufficient_size;
    }
    (void)span_copy(remaining, json_text, NULL);

    /* The last token is the writer's new state. need_comma is true since the
     * fragment is a complete value. */
    writer_advance(writer, required, true, last_kind);
    return ok;
}
