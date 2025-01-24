#ifndef SOCKET_STREAM_H
#define SOCKET_STREAM_H

#include "niceties.h"
#include "stream.h"
#include "socket.h"

result_t socket_stream_initialize(stream_t* stream, socket_t* socket);

#endif // SOCKET_STREAM_H