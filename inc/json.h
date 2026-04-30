/* SPDX-License-Identifier: MIT
 *
 * Deterministic JSON reader/writer for common-lib-c.
 *
 * Derived from azure-sdk-for-c (Copyright (c) Microsoft Corporation, MIT
 * License) and adapted to common-lib-c types and conventions:
 *   - span_t (uint32_t size) instead of az_span (int32_t size)
 *   - result_t instead of az_result
 *   - single contiguous buffer (no chunked / non-contiguous mode)
 *   - no precondition layer; invalid arguments return invalid_argument
 *
 * Original source:
 *   https://github.com/Azure/azure-sdk-for-c (sdk/inc/azure/core/az_json.h)
 */

#ifndef COMMON_LIB_C_JSON_H
#define COMMON_LIB_C_JSON_H

#include <stdbool.h>
#include <stdint.h>
#include <span.h>
#include <niceties.h>

/* Maximum nesting depth supported by the writer/reader bit stack. */
#define JSON_MAX_NESTING_DEPTH 64

/* Token kinds returned by the reader and tracked by the writer. */
typedef enum
{
    json_token_none = 0,
    json_token_begin_object,
    json_token_end_object,
    json_token_begin_array,
    json_token_end_array,
    json_token_property_name,
    json_token_string,
    json_token_number,
    json_token_true,
    json_token_false,
    json_token_null
} json_token_kind_t;

/* Internal bit stack tracking object/array nesting (1 bit per level). */
typedef struct
{
    uint64_t stack;
    uint32_t current_depth;
} json_bit_stack_t;

/* ============================== JSON WRITER ============================== */

typedef struct
{
    /* Reserved for future options; ignored today. */
    uint8_t reserved;
} json_writer_options_t;

typedef struct json_writer
{
    /* Total number of bytes written to the destination so far. Read-only. */
    uint32_t total_bytes_written;

    struct
    {
        span_t            destination;
        uint32_t          bytes_written;
        bool              need_comma;
        json_token_kind_t token_kind;
        json_bit_stack_t  bit_stack;
    } _internal;
} json_writer_t;

/**
 * @brief Initializes a writer to emit JSON into the destination buffer.
 *
 * @param[out] out_writer   Writer to initialize.
 * @param[in]  destination  Buffer to write into.
 * @param[in]  options      Optional. May be NULL.
 *
 * @retval ok                 Initialized successfully.
 * @retval invalid_argument   out_writer is NULL.
 */
result_t json_writer_init(json_writer_t* out_writer, span_t destination, json_writer_options_t const* options);

/**
 * @brief Returns the slice of the destination buffer containing the JSON
 *        text written so far.
 */
static inline span_t json_writer_get_bytes_written(json_writer_t const* writer)
{
    return span_slice(writer->_internal.destination, 0, writer->_internal.bytes_written);
}

result_t json_writer_append_string         (json_writer_t* writer, span_t value);
result_t json_writer_append_property_name  (json_writer_t* writer, span_t name);
result_t json_writer_append_bool           (json_writer_t* writer, bool value);
result_t json_writer_append_int32          (json_writer_t* writer, int32_t value);
result_t json_writer_append_double         (json_writer_t* writer, double value, uint32_t fractional_digits);
result_t json_writer_append_null           (json_writer_t* writer);
result_t json_writer_append_begin_object   (json_writer_t* writer);
result_t json_writer_append_end_object     (json_writer_t* writer);
result_t json_writer_append_begin_array    (json_writer_t* writer);
result_t json_writer_append_end_array      (json_writer_t* writer);

/**
 * @brief Appends a pre-validated JSON text fragment as-is. The fragment is
 *        validated against RFC 8259 before being appended; if appending the
 *        fragment would produce an invalid document, invalid_state is
 *        returned. Available once the reader is implemented (Phase 2).
 */
result_t json_writer_append_json_text(json_writer_t* writer, span_t json_text);

/* ============================== JSON READER ============================== */

typedef struct
{
    uint8_t reserved;
} json_reader_options_t;

/**
 * @brief A JSON token produced by the reader.
 *
 * For string and property name tokens, `slice` does NOT include the
 * surrounding quotes. For numbers, `slice` is the full numeric literal.
 * For containers and literals, `slice` is the single byte / literal text.
 */
typedef struct
{
    json_token_kind_t kind;
    /* The slice of the source JSON corresponding to this token. */
    span_t            slice;
    /* For strings, indicates whether `slice` contains escape sequences. */
    bool              string_has_escaped_chars;
} json_token_t;

typedef struct json_reader
{
    json_token_t token;
    /* Recursive depth of the current token (read-only). */
    uint32_t     current_depth;

    struct
    {
        span_t           json_buffer;
        uint32_t         bytes_consumed;
        bool             is_complex_json;
        json_bit_stack_t bit_stack;
    } _internal;
} json_reader_t;

/**
 * @brief Initializes a reader to parse the JSON contained in `json_buffer`.
 *
 * @retval ok                Reader initialized.
 * @retval invalid_argument  out_reader is NULL or json_buffer is empty.
 */
result_t json_reader_init(json_reader_t* out_reader, span_t json_buffer, json_reader_options_t const* options);

/**
 * @brief Reads the next token. After a successful call, `reader->token` is
 *        updated.
 *
 * @retval ok                 Token read successfully.
 * @retval json_reader_done   No more tokens to process (success).
 * @retval unexpected_char    Invalid character encountered.
 * @retval unexpected_end     Input ended mid-token.
 * @retval nesting_overflow   Container nesting exceeded JSON_MAX_NESTING_DEPTH.
 * @retval invalid_state      Reader is in an unrecoverable state.
 */
result_t json_reader_next_token(json_reader_t* reader);

/**
 * @brief Skips over the children of the current token. If the current token
 *        is a property name, advances to its value first; if the current
 *        token (after that step) starts a container, advances past its
 *        matching close. Otherwise no-op.
 */
result_t json_reader_skip_children(json_reader_t* reader);

/* ============================== TOKEN GETTERS ============================ */

/**
 * @brief Reads a boolean from a token of kind json_token_true / json_token_false.
 * @retval ok                    Value extracted.
 * @retval invalid_state         Token kind is not true/false.
 */
result_t json_token_get_boolean(json_token_t const* token, bool* out_value);

/**
 * @brief Reads a signed/unsigned integer from a json_token_number.
 *
 * Fractional/exponent components are rejected (use json_token_get_double for
 * those). Out-of-range values return unexpected_char.
 *
 * @retval ok                    Value extracted.
 * @retval invalid_state         Token is not a number.
 * @retval unexpected_char       Token contains non-digit characters or
 *                               overflows the destination type.
 */
result_t json_token_get_int32 (json_token_t const* token, int32_t*  out_value);
result_t json_token_get_uint32(json_token_t const* token, uint32_t* out_value);
result_t json_token_get_int64 (json_token_t const* token, int64_t*  out_value);
result_t json_token_get_uint64(json_token_t const* token, uint64_t* out_value);

/**
 * @brief Reads a double from a json_token_number. Accepts the full JSON
 *        number grammar including fraction and exponent. Locale-independent.
 */
result_t json_token_get_double(json_token_t const* token, double* out_value);

/**
 * @brief Copies the unescaped UTF-8 string for a json_token_string into
 *        `destination`, which must be large enough to hold the unescaped
 *        result. Always NUL-terminates if there is room.
 *
 * @param[out] out_length   Optional. Set to the unescaped length (excluding NUL).
 *
 * @retval ok                    String copied.
 * @retval invalid_state         Token is not a string.
 * @retval insufficient_size     `destination` is too small.
 * @retval unexpected_char       Malformed escape sequence in token.
 */
result_t json_token_get_string(json_token_t const* token, span_t destination, uint32_t* out_length);

/**
 * @brief Compares the unescaped string content of a json_token_string or
 *        json_token_property_name against `expected_text`. Other token
 *        kinds always return false.
 */
bool json_token_is_text_equal(json_token_t const* token, span_t expected_text);

/**
 * @brief Standalone helper to unescape an arbitrary JSON string body (without
 *        surrounding quotes). Returns the unescaped slice of `destination`,
 *        or SPAN_EMPTY if the destination is too small or the input is
 *        malformed.
 */
span_t json_string_unescape(span_t json_string, span_t destination);

#endif /* COMMON_LIB_C_JSON_H */
