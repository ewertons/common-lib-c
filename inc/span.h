#ifndef SPAN_H
#define SPAN_H

#include <stddef.h>
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
* @remarks Finds the position of a span within another span.
* 
* @return Zero if found, or -1 if not.
*/
int span_find(span_t span, int32_t start, span_t target, span_t* out_found);

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

int span_split(span_t span, uint32_t start, span_t delimiter, span_t* left, span_t* right); 

#endif // SPAN_H
