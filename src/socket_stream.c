#include <stdlib.h>
#include "socket_stream.h"

static result_t socket_stream_open(struct stream* stream)
{
    socket_t* socket = (socket_t*)stream->inner_stream;

    return socket_connect(socket);
}

static result_t socket_stream_close(struct stream* stream)
{
    socket_t* socket = (socket_t*)stream->inner_stream;

    return socket_deinit(socket);
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
        stream->inner_stream = socket;

        result = ok;
    }

    return result;
}
