/* SPDX-License-Identifier: MIT
 *
 * Private types and helpers shared between json_writer.c and json_reader.c.
 *
 * Derived from azure-sdk-for-c az_json_private.h (Copyright (c) Microsoft
 * Corporation, MIT License).
 */

#ifndef COMMON_LIB_C_JSON_PRIVATE_H
#define COMMON_LIB_C_JSON_PRIVATE_H

#include <stdbool.h>
#include <stdint.h>
#include <json.h>

/* Cap on input string size, mirroring az's ~half of INT_MAX. */
#define JSON_MAX_ESCAPED_STRING_SIZE             1000000000U

/* Worst-case expansion: 1 ASCII byte -> 6 bytes (\uXXXX). */
#define JSON_MAX_EXPANSION_FACTOR_WHILE_ESCAPING 6U

#define JSON_MAX_UNESCAPED_STRING_SIZE \
    (JSON_MAX_ESCAPED_STRING_SIZE / JSON_MAX_EXPANSION_FACTOR_WHILE_ESCAPING)

/* Max digits we will format after the decimal point. */
#define JSON_MAX_SUPPORTED_FRACTIONAL_DIGITS     15U

/* [-][0-9]{16}.[0-9]{15} = 33 bytes maximum for a double. */
#define JSON_MAX_SIZE_FOR_WRITING_DOUBLE         33U

/* Maximum size for an int32 in decimal: "-2147483648" = 11 bytes. */
#define JSON_MAX_SIZE_FOR_INT32                  11U

#define JSON_ASCII_SPACE_CHARACTER               0x20U
#define JSON_NUMBER_OF_HEX_VALUES                16U

/* Bit-stack values: 1 = object, 0 = array. */
typedef enum
{
    json_stack_array  = 0,
    json_stack_object = 1
} json_stack_item_t;

static inline void json_stack_push(json_bit_stack_t* s, json_stack_item_t item)
{
    s->current_depth++;
    s->stack <<= 1U;
    s->stack |= (uint32_t)item;
}

static inline json_stack_item_t json_stack_pop(json_bit_stack_t* s)
{
    json_stack_item_t top = (json_stack_item_t)(s->stack & 1U);
    if (s->current_depth != 0)
    {
        s->stack >>= 1U;
        s->current_depth--;
    }
    return top;
}

static inline json_stack_item_t json_stack_peek(json_bit_stack_t const* s)
{
    return (json_stack_item_t)(s->stack & 1U);
}

/* Convert a nibble 0..15 to its uppercase hex character. */
static inline uint8_t json_number_to_upper_hex(uint8_t n)
{
    return (uint8_t)(n + (n < 10U ? '0' : ('A' - 10)));
}

#endif /* COMMON_LIB_C_JSON_PRIVATE_H */
