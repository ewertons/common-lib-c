#include <stdlib.h>
#include "socket_stream.h"

static result_t socket_stream_open(struct stream* stream)
{
    socket_t* socket = (socket_t*)stream;

    return socket_connect(socket);
}

static result_t socket_stream_close(struct stream* stream)
{
    socket_t* socket = (socket_t*)stream;

    return socket_deinit(socket);
}

static result_t socket_stream_write(struct stream* stream, span_t data, span_t* remainder)
{
    (void)remainder;
    return socket_write((socket_t*)stream, data); // TODO: change socket_write to return remainder.
}

static result_t socket_stream_read(struct stream* stream, span_t data, span_t* read, span_t* remainder)
{
    return socket_read((socket_t*)stream, data, read, remainder);
}

result_t socket_stream_initialize(stream_t* stream, socket_t* socket)
{
    result_t result;

    if (stream == NULL || socket == NULL)
    {
        result = invalid_argument;
    }
    else
    {
        stream->open = socket_stream_open;
        stream->close = socket_stream_close;
        stream->write = socket_stream_write;
        stream->read = socket_stream_read;
        stream->inner_stream = socket;

        result = ok;
    }

    return result;
}
