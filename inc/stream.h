#ifndef STREAM_H
#define STREAM_H

#include "span.h"

struct stream;

typedef result_t (*stream_open_function_t)(struct stream* stream);
typedef result_t (*stream_close_function_t)(struct stream* stream);
typedef result_t (*stream_write_function_t)(struct stream* stream, span_t data, span_t* remainder);
typedef result_t (*stream_read_function_t)(struct stream* stream, span_t data, span_t* read, span_t* remainder);

typedef struct stream
{
    stream_open_function_t open;
    stream_close_function_t close;
    stream_write_function_t write;
    stream_read_function_t read;
    void* inner_stream;
} stream_t;

static inline result_t stream_open(stream_t* stream)
{
    return stream->open(stream->inner_stream);
}

static inline result_t stream_close(stream_t* stream)
{
    return stream->close(stream->inner_stream);
}

static inline result_t stream_write(stream_t* stream, span_t data, span_t* remainder)
{
    return stream->write(stream->inner_stream, data, remainder);
}

/**
 * @brief Reads bytes from a stream into the destination buffer.
 * 
 * @param stream #stream_t instance to read bytes from.
 * @param buffer #span_t buffer where to write bytes into.
 * @param read #span_t for the slice of \p buffer where the bytes are written.
 * @param remainder The #span_t containing the unused remainder of \p buffer.
 * @return result_t 
 */
static inline result_t stream_read(stream_t* stream, span_t buffer, span_t* read, span_t* remainder)
{
    return stream->read(stream->inner_stream, buffer, read, remainder);
}

#endif // STREAM_H
