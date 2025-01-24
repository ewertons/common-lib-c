#ifndef STREAM_H
#define STREAM_H

#include "span.h"

struct stream;

typedef result_t (*stream_open_function_t)(struct stream* stream);
typedef result_t (*stream_close_function_t)(struct stream* stream);

typedef struct stream
{
    stream_open_function_t open;
    stream_close_function_t close;
    void* inner_stream;
} stream_t;

result_t stream_open(stream_t* stream);

result_t stream_write(stream_t* stream, span_t data, span_t* remainder);

result_t stream_read(stream_t* stream, span_t data, span_t* read);

result_t stream_close(stream_t* stream);

#endif // STREAM_H