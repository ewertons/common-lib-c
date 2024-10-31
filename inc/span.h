#ifndef SPAN_H
#define SPAN_H

#include <stddef.h>
#include <stdbool.h>
#include <inttypes.h>
#include <niceties.h>

typedef struct span
{
    uint8_t* ptr;
    uint32_t length;
} span_t;

#define SPAN_EMPTY \
  (span_t){ .ptr = (uint8_t*)NULL, .length = 0 }

static inline uint8_t* span_get_ptr(span_t s)
{
    return s.ptr;
}

static inline uint32_t span_get_size(span_t s)
{
    return s.length;
}

static inline span_t span_init(uint8_t* ptr, uint32_t length)
{
    return (span_t){ .ptr = ptr, .length = length };
}

static inline uint8_t span_get(span_t s, uint32_t p)
{
    return s.ptr[p];
}

#define span_from_memory(x) span_init(x, sizeofarray(x))

#define span_from_string(x) span_init(x, sizeofarray(x) - 1)

#define span_from_str_literal(X) \
  (span_t){ \
      .ptr = (uint8_t*)X, \
      .length = strlitlen(X) \
  }

int span_compare(span_t a, span_t b);

/**
 * @brief Verifies if an span is empty (its size is zero).
 * 
 * @param span The #span_t to be verified.
 * @return true If the size of \p span is zero.
 * @return false If the size of \p span is not zero.
 */
static inline bool span_is_empty(span_t span)
{
    return (span_get_size(span) == 0);
}

/**
 * @brief Finds a #span_t in another #span_t.
 * 
 * @param span      #span_t in which the seach will be performed.
 * @param start     Position in \p span to start searching for \p target.
 * @param target    #span_t to search for in \p span
 * @param out_remainder If token is found, \p out_remainder is set as a #span_t that starts
 *                  after \p target in \p span to the end of \p span.
 * @return          The position of \p target in \p span if found or -1 if not found.
 */
int span_find(span_t span, int32_t start, span_t target, span_t* out_remainder);

/**
 * @brief Iterates through a #span_t splitting it using \p delimiter until \p span is empty.
 * 
 * @param span          The #span_t to iterate through.
 * @param delimiter     The token used to split \p span.
 * @param out_item      A #span_t where to store either the chunk of \p span that comes before \p delimiter or
 *                      the whole \p span if \p delimiter is not found but \p span is not empty. 
 * @param out_remainder To store the chunk of \p span after \p delimiter (if found in \p span ) or SPAN_EMPTY
 *                      if \p delimiter is not found (if \p span is not empty).
 * @return              #invalid_argument if \p delimiter is empty, or if \p out_item or \p remainder are NULL.
 * @return              #end_of_data if \p span is empty (#SPAN_EMPTY).
 * @return              #ok if \p out_item can be set ( \p delimiter is found or \p span is not empty).
 */
result_t span_iterate(span_t span, span_t delimiter, span_t* out_item, span_t* out_remainder);

static inline span_t span_slice(span_t span, uint32_t start, uint32_t size)
{
    if (size == 0 || ((start + size) > span_get_size(span)))
    {
        return SPAN_EMPTY;
    }

    return span_init(span_get_ptr(span) + start, size);
}

static inline span_t span_slice_to_end(span_t span, uint32_t start)
{
    return span_slice(span, start, span.length - start);
}

int span_to_uint32_t(span_t span, uint32_t* value);

/**
 * @brief Finds a token in a #span_t and returns the portions left and right of it separately.
 * 
 * @param span The #span where to search for \p delimiter and extract \p left and \p right.
 * @param start The position in \p span to start searching for \p delimiter.  
 * @param delimiter The token to search for in \p span.
 * @param left A #span_t in \p span starting from \p start up to before where \p delimiter is found. This is optional.
 * @param right A #span_t in \p span starting from after \p delimiter is found all the way to the end of \p span. This is optional.
 * @return Zero if \p delimiter is found, or non-zero otherwise.
 */
int span_split(span_t span, uint32_t start, span_t delimiter, span_t* left, span_t* right); 

static inline span_t span_grab(span_t from, uint32_t size, span_t* out_remainder)
{
    span_t result = span_slice(from, 0, size);

    if (out_remainder != NULL)
    {
        *out_remainder = span_slice_to_end(from, size);
    }

    return result;
}

/**
 * @brief Copy the content from \p from into \p to.
 * 
 * @param[out]  to         #span_t where to copy the content from.
 * @param[in]   from       #span_t where to write the content of \p to.
 * @param[out]  remainder  #span_t where to store the unused remainder of  \p to.
 * @return                 A #span_t in \p to with the size of \p from if succeeds, or #SPAN_EMPTY if it fails.
 */
span_t span_copy(span_t to, span_t from, span_t* remainder);

span_t span_copy_n(span_t to, span_t* from, int32_t count, int32_t* required_size, span_t* remainder);

#endif // SPAN_H
