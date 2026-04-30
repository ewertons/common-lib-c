/* SPDX-License-Identifier: MIT
 *
 * Token typed getters and string unescape for common-lib-c JSON.
 *
 * Derived from azure-sdk-for-c az_json_token.c (Copyright (c) Microsoft
 * Corporation, MIT License). Adaptations:
 *   - span_t (uint32_t size) instead of az_span (int32_t size)
 *   - result_t instead of az_result
 *   - locale-independent number parsers (no strtod / strtoll dependencies)
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <json.h>
#include <span.h>
#include <niceties.h>

#include "json_private.h"

/* -------------------------------------------------------------------------- */
/* Boolean                                                                    */
/* -------------------------------------------------------------------------- */

result_t json_token_get_boolean(json_token_t const* token, bool* out_value)
{
    if (token == NULL || out_value == NULL) return invalid_argument;
    if (token->kind == json_token_true)  { *out_value = true;  return ok; }
    if (token->kind == json_token_false) { *out_value = false; return ok; }
    return invalid_state;
}

/* -------------------------------------------------------------------------- */
/* Integer parsers (locale-independent, overflow-checked).                    */
/* -------------------------------------------------------------------------- */

static bool digit_value(uint8_t c, uint8_t* out)
{
    if (c >= '0' && c <= '9') { *out = (uint8_t)(c - '0'); return true; }
    return false;
}

/* Parse integer digits from p[i..size) into a uint64. Stops at first
 * non-digit. Returns false if parsing reads zero digits, or if the value
 * overflows uint64. */
static bool parse_uint64_digits(uint8_t const* p, uint32_t size, uint32_t* idx, uint64_t* out)
{
    uint64_t v = 0;
    uint32_t i = *idx;
    uint32_t start = i;
    uint8_t  d;

    while (i < size && digit_value(p[i], &d))
    {
        if (v > (UINT64_MAX - d) / 10U) return false;
        v = v * 10U + d;
        i++;
    }
    if (i == start) return false;

    *idx = i;
    *out = v;
    return true;
}

/* Reject fractional/exponent components for integer getters. */
static bool token_is_pure_integer(span_t s, bool* out_negative, uint64_t* out_magnitude)
{
    uint8_t* p    = span_get_ptr(s);
    uint32_t size = span_get_size(s);
    uint32_t i    = 0;

    if (size == 0) return false;

    bool negative = false;
    if (p[0] == '-') { negative = true; i = 1; }

    uint64_t v;
    if (!parse_uint64_digits(p, size, &i, &v)) return false;

    /* No fractional or exponent allowed. */
    if (i != size) return false;

    *out_negative  = negative;
    *out_magnitude = v;
    return true;
}

result_t json_token_get_uint64(json_token_t const* token, uint64_t* out_value)
{
    if (token == NULL || out_value == NULL) return invalid_argument;
    if (token->kind != json_token_number) return invalid_state;

    bool     neg;
    uint64_t mag;
    if (!token_is_pure_integer(token->slice, &neg, &mag)) return unexpected_char;
    if (neg && mag != 0) return unexpected_char;

    *out_value = mag;
    return ok;
}

result_t json_token_get_uint32(json_token_t const* token, uint32_t* out_value)
{
    uint64_t v;
    result_t r = json_token_get_uint64(token, &v);
    if (r != ok) return r;
    if (v > UINT32_MAX) return unexpected_char;
    *out_value = (uint32_t)v;
    return ok;
}

result_t json_token_get_int64(json_token_t const* token, int64_t* out_value)
{
    if (token == NULL || out_value == NULL) return invalid_argument;
    if (token->kind != json_token_number) return invalid_state;

    bool     neg;
    uint64_t mag;
    if (!token_is_pure_integer(token->slice, &neg, &mag)) return unexpected_char;

    if (neg)
    {
        /* Magnitude limit: |INT64_MIN| = 2^63. */
        if (mag > (uint64_t)INT64_MAX + 1ULL) return unexpected_char;
        if (mag == (uint64_t)INT64_MAX + 1ULL) { *out_value = INT64_MIN; return ok; }
        *out_value = -(int64_t)mag;
    }
    else
    {
        if (mag > (uint64_t)INT64_MAX) return unexpected_char;
        *out_value = (int64_t)mag;
    }
    return ok;
}

result_t json_token_get_int32(json_token_t const* token, int32_t* out_value)
{
    int64_t v;
    result_t r = json_token_get_int64(token, &v);
    if (r != ok) return r;
    if (v < INT32_MIN || v > INT32_MAX) return unexpected_char;
    *out_value = (int32_t)v;
    return ok;
}

/* -------------------------------------------------------------------------- */
/* Double parser (locale-independent).                                        */
/* -------------------------------------------------------------------------- */

result_t json_token_get_double(json_token_t const* token, double* out_value)
{
    if (token == NULL || out_value == NULL) return invalid_argument;
    if (token->kind != json_token_number) return invalid_state;

    uint8_t* p    = span_get_ptr(token->slice);
    uint32_t size = span_get_size(token->slice);
    uint32_t i    = 0;
    if (size == 0) return unexpected_char;

    bool negative = false;
    if (p[i] == '-') { negative = true; i++; if (i >= size) return unexpected_char; }

    /* Integer part. */
    double  value         = 0.0;
    bool    saw_int_digit = false;
    if (p[i] == '0')
    {
        saw_int_digit = true;
        i++;
    }
    else
    {
        uint8_t d;
        while (i < size && digit_value(p[i], &d))
        {
            value = value * 10.0 + (double)d;
            saw_int_digit = true;
            i++;
        }
    }
    if (!saw_int_digit) return unexpected_char;

    /* Fractional part. */
    if (i < size && p[i] == '.')
    {
        i++;
        double scale = 0.1;
        bool   saw   = false;
        uint8_t d;
        while (i < size && digit_value(p[i], &d))
        {
            value += (double)d * scale;
            scale *= 0.1;
            saw    = true;
            i++;
        }
        if (!saw) return unexpected_char;
    }

    /* Exponent. */
    if (i < size && (p[i] == 'e' || p[i] == 'E'))
    {
        i++;
        if (i >= size) return unexpected_char;
        bool exp_neg = false;
        if (p[i] == '+') { i++; }
        else if (p[i] == '-') { exp_neg = true; i++; }

        if (i >= size) return unexpected_char;

        int32_t exp = 0;
        bool    saw = false;
        uint8_t d;
        while (i < size && digit_value(p[i], &d))
        {
            if (exp > (INT32_MAX - d) / 10) return unexpected_char;
            exp = exp * 10 + (int32_t)d;
            saw = true;
            i++;
        }
        if (!saw) return unexpected_char;

        /* Apply exponent via repeated multiplication by 10 (deterministic;
         * pow() may not be exact for some inputs but suffices for typical
         * JSON exponents). */
        double base = exp_neg ? 0.1 : 10.0;
        for (int32_t e = 0; e < exp; e++)
        {
            value *= base;
        }
    }

    if (i != size) return unexpected_char;
    if (!isfinite(value)) return unexpected_char;

    *out_value = negative ? -value : value;
    return ok;
}

/* -------------------------------------------------------------------------- */
/* String unescape                                                            */
/* -------------------------------------------------------------------------- */

/* Convert one hex digit to its value 0..15. Returns -1 on invalid. */
static int hex_value(uint8_t c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Encode a Unicode code point as UTF-8 into 'out' (up to 4 bytes). Returns
 * the number of bytes written, or 0 on invalid code point or too little
 * destination space. */
static uint32_t encode_utf8(uint32_t cp, span_t* out)
{
    uint8_t buf[4];
    uint32_t n;

    if (cp < 0x80U)
    {
        buf[0] = (uint8_t)cp;
        n = 1;
    }
    else if (cp < 0x800U)
    {
        buf[0] = (uint8_t)(0xC0U | (cp >> 6));
        buf[1] = (uint8_t)(0x80U | (cp & 0x3FU));
        n = 2;
    }
    else if (cp < 0x10000U)
    {
        buf[0] = (uint8_t)(0xE0U | (cp >> 12));
        buf[1] = (uint8_t)(0x80U | ((cp >> 6) & 0x3FU));
        buf[2] = (uint8_t)(0x80U | (cp & 0x3FU));
        n = 3;
    }
    else if (cp < 0x110000U)
    {
        buf[0] = (uint8_t)(0xF0U | (cp >> 18));
        buf[1] = (uint8_t)(0x80U | ((cp >> 12) & 0x3FU));
        buf[2] = (uint8_t)(0x80U | ((cp >> 6) & 0x3FU));
        buf[3] = (uint8_t)(0x80U | (cp & 0x3FU));
        n = 4;
    }
    else
    {
        return 0;
    }

    if (span_get_size(*out) < n) return 0;
    (void)memcpy(span_get_ptr(*out), buf, n);
    *out = span_slice_to_end(*out, n);
    return n;
}

/* Read a 4-hex-digit unit at p[i..i+4). Advances *idx past the digits.
 * Returns false on malformed input. */
static bool read_hex4(uint8_t const* p, uint32_t size, uint32_t* idx, uint16_t* out)
{
    if (*idx + 4U > size) return false;
    uint32_t v = 0;
    for (uint32_t k = 0; k < 4; k++)
    {
        int h = hex_value(p[*idx + k]);
        if (h < 0) return false;
        v = (v << 4) | (uint32_t)h;
    }
    *idx += 4;
    *out = (uint16_t)v;
    return true;
}

/* Core unescape: writes unescaped bytes from [p..p+size) to *out, advancing
 * *out past the written region. Returns true on success. */
static bool unescape_into(uint8_t const* p, uint32_t size, span_t* out)
{
    uint32_t i = 0;
    while (i < size)
    {
        uint8_t c = p[i];
        if (c != '\\')
        {
            if (span_get_size(*out) < 1U) return false;
            span_t rem;
            if (span_copy_u8(*out, c, &rem) != 0) return false;
            *out = rem;
            i++;
            continue;
        }

        /* Escape sequence. */
        i++;
        if (i >= size) return false;
        uint8_t e = p[i++];
        uint8_t literal = 0;
        switch (e)
        {
            case '"':  literal = '"';  break;
            case '\\': literal = '\\'; break;
            case '/':  literal = '/';  break;
            case 'b':  literal = '\b'; break;
            case 'f':  literal = '\f'; break;
            case 'n':  literal = '\n'; break;
            case 'r':  literal = '\r'; break;
            case 't':  literal = '\t'; break;
            case 'u':
            {
                uint16_t unit;
                if (!read_hex4(p, size, &i, &unit)) return false;

                uint32_t cp;
                if (unit >= 0xD800U && unit <= 0xDBFFU)
                {
                    /* High surrogate: expect '\u' low surrogate next. */
                    if (i + 6U > size) return false;
                    if (p[i] != '\\' || p[i + 1U] != 'u') return false;
                    i += 2;
                    uint16_t low;
                    if (!read_hex4(p, size, &i, &low)) return false;
                    if (low < 0xDC00U || low > 0xDFFFU) return false;
                    cp = 0x10000U
                        + (((uint32_t)unit - 0xD800U) << 10)
                        + ((uint32_t)low  - 0xDC00U);
                }
                else if (unit >= 0xDC00U && unit <= 0xDFFFU)
                {
                    /* Lone low surrogate -> invalid. */
                    return false;
                }
                else
                {
                    cp = unit;
                }

                if (encode_utf8(cp, out) == 0) return false;
                continue;
            }
            default:
                return false;
        }

        if (span_get_size(*out) < 1U) return false;
        span_t rem;
        if (span_copy_u8(*out, literal, &rem) != 0) return false;
        *out = rem;
    }
    return true;
}

span_t json_string_unescape(span_t json_string, span_t destination)
{
    span_t out = destination;
    if (!unescape_into(span_get_ptr(json_string), span_get_size(json_string), &out))
    {
        return SPAN_EMPTY;
    }
    uint32_t written = span_get_size(destination) - span_get_size(out);
    return span_slice(destination, 0, written);
}

result_t json_token_get_string(json_token_t const* token, span_t destination, uint32_t* out_length)
{
    if (token == NULL) return invalid_argument;
    if (token->kind != json_token_string && token->kind != json_token_property_name)
    {
        return invalid_state;
    }

    /* Fast path: no escapes. */
    if (!token->string_has_escaped_chars)
    {
        if (span_get_size(destination) < span_get_size(token->slice))
        {
            return insufficient_size;
        }
        if (span_get_size(token->slice) > 0)
        {
            (void)memcpy(span_get_ptr(destination),
                         span_get_ptr(token->slice),
                         span_get_size(token->slice));
        }
        if (out_length != NULL) *out_length = span_get_size(token->slice);
        return ok;
    }

    span_t out = destination;
    if (!unescape_into(span_get_ptr(token->slice), span_get_size(token->slice), &out))
    {
        /* Distinguish "out of space" from "malformed". The reader pre-validated
         * escape sequences, so the only realistic failure is space. */
        return insufficient_size;
    }

    uint32_t written = span_get_size(destination) - span_get_size(out);
    if (out_length != NULL) *out_length = written;
    return ok;
}

bool json_token_is_text_equal(json_token_t const* token, span_t expected_text)
{
    if (token == NULL) return false;
    if (token->kind != json_token_string && token->kind != json_token_property_name)
    {
        return false;
    }

    if (!token->string_has_escaped_chars)
    {
        return span_compare(token->slice, expected_text) == 0;
    }

    /* Unescape on the fly comparing byte-by-byte against expected_text. */
    uint8_t  scratch[64];
    span_t   exp_remaining = expected_text;
    uint8_t* src           = span_get_ptr(token->slice);
    uint32_t src_size      = span_get_size(token->slice);
    uint32_t i             = 0;

    while (i < src_size)
    {
        span_t buf = span_from_memory(scratch);
        span_t out = buf;
        /* Process up to a chunk's worth of source at a time. We bound the
         * source we feed into unescape_into so we don't overflow the scratch
         * buffer; in practice, chunks of <= scratch_size/6 source bytes are
         * always safe (worst-case 6x expansion is for \uXXXX -> 4 bytes UTF-8,
         * but a literal byte writes 1, and \\X writes 1 -- so 1:1 worst case
         * for non-Unicode escapes; for \uXXXX we read 6 source -> write 4).
         * Use 16 source bytes per pass, which can produce at most 16 output
         * bytes. */
        uint32_t chunk = src_size - i;
        if (chunk > 16U) chunk = 16U;

        /* Find a safe chunk boundary that doesn't split an escape. */
        uint32_t safe = 0;
        while (safe < chunk)
        {
            uint8_t c = src[i + safe];
            if (c == '\\')
            {
                /* Escape: consume \ + one byte (or \uXXXX = 6 bytes total). */
                if (safe + 1U >= chunk) break;
                if (src[i + safe + 1U] == 'u')
                {
                    if (safe + 6U > chunk) break;
                    /* Surrogate pair may need 12 bytes; bail out and let the
                     * next iteration handle it after expanding the chunk. */
                    if (chunk - safe < 12U && (i + safe + 12U) <= src_size)
                    {
                        /* Just process up to safe so far. */
                        break;
                    }
                    safe += 6U;
                }
                else
                {
                    safe += 2U;
                }
            }
            else
            {
                safe += 1U;
            }
        }
        if (safe == 0) safe = chunk;  /* avoid infinite loop on truncated tail */

        if (!unescape_into(src + i, safe, &out)) return false;

        uint32_t produced = span_get_size(buf) - span_get_size(out);
        if (produced > span_get_size(exp_remaining)) return false;
        if (produced > 0 &&
            memcmp(span_get_ptr(buf), span_get_ptr(exp_remaining), produced) != 0)
        {
            return false;
        }
        exp_remaining = span_slice_to_end(exp_remaining, produced);
        i += safe;
    }

    return span_get_size(exp_remaining) == 0;
}
