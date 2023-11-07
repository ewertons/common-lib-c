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

static inline uint8_t* span_get_ptr(span_t* s)
{
    return s->ptr;
}

static inline uint32_t span_get_length(span_t* s)
{
    return s->length;
}

static inline span_t span_init(uint8_t* ptr, uint32_t length)
{
    return (span_t){ .ptr = ptr, .length = length };
}

static inline uint8_t span_get(span_t* s, uint32_t p)
{
    return s->ptr[p];
}

#define span_from_string(x) span_init(x, sizeofarray(x) - 1)

#define span_from_str_literal(X) \
  (span_t){ \
      .ptr = (uint8_t*)X, \
      .length = strlitlen(X) \
  }

int span_compare(span_t a, span_t b);

int span_find(span_t* span, uint32_t start, span_t target, uint32_t* pos);

static inline span_t span_slice(span_t* span, uint32_t start, uint32_t end)
{
    return span_init(span_get_ptr(span) + start, end - start);
}

int span_to_uint32_t(span_t span, uint32_t* value);

#endif // SPAN_H
