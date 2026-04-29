#ifndef SOCKET_H
#define SOCKET_H

#include <stdlib.h>
#include <stdbool.h>

#include "span.h"
#include "niceties.h"
#include "task.h"

#include <arpa/inet.h>
#include <sys/socket.h>

/* ------------------------------------------------------------------------- *
 * TLS backend abstraction.
 *
 * The header intentionally does NOT pull in <openssl/ssl.h> (or any other
 * TLS implementation header). All implementation-specific state lives in
 * an opaque #tls_backend_t struct whose definition is private to the
 * backend translation unit (e.g. src/socket.c for the OpenSSL backend, or
 * a future src/socket_mbedtls.c).
 *
 * Consequences:
 *   - Consumers of socket.h compile without an OpenSSL/mbedTLS toolchain
 *     in their include path.
 *   - The library can be compiled into either a static archive linked
 *     against a specific TLS backend, or a shared object that selects a
 *     backend at link time -- the ABI exposed by socket.h is the same
 *     either way because the only TLS-related field on the public type
 *     is a `void*`-sized opaque pointer.
 *
 * Backend selection (build-time):
 *   - SOCKET_TLS_OPENSSL  -- default; links against libssl/libcrypto.
 *   - SOCKET_TLS_MBEDTLS  -- (future) drop-in replacement.
 *   - SOCKET_TLS_NONE     -- (future) plain-TCP-only build, no TLS at
 *                            all; tls.enabled=true returns error.
 * ------------------------------------------------------------------------- */
typedef struct tls_backend tls_backend_t;

#define DEFAULT_LISTENING_PORT 8234

typedef enum socket_role
{
    socket_role_client,
    socket_role_server
} socket_role_t;

typedef struct local_host_config
{
    int port;
} local_host_config_t;

typedef struct remote_host_config
{
    span_t hostname;
    int port;
} remote_host_config_t;

typedef struct socket_config
{
    socket_role_t role;

    local_host_config_t local;
    remote_host_config_t remote;

    struct
    {
        bool enable;
        const char* certificate_file;
        const char* private_key_file;
        const char* trusted_certificate_file;
    } tls;
} socket_config_t;

typedef struct socket
{
    socket_role_t role;
    local_host_config_t local;
    remote_host_config_t remote;

    int listen_sd;
    int sd;
    struct sockaddr_in sa_serv;
    struct sockaddr_in sa_cli;
    socklen_t client_len;

    /* Non-blocking driver state. `io_want` is the bitmask of
     * #socket_io_want_t the last NB operation reported. `tcp_connected`
     * indicates the connect() phase finished for clients. */
    uint32_t io_want;
    bool     tcp_connected;

    /* All TLS-related state, grouped together so consumers can reason
     * about it as one unit (`s.tls.enabled`, `s.tls.handshake_done`).
     * The `backend` pointer is opaque and owned by whichever TLS backend
     * is linked into the build; treat it as private to socket.c. */
    struct
    {
        bool            enabled;
        bool            handshake_done;
        tls_backend_t*  backend;
    } tls;

    struct socket* parent;
} socket_t;

static inline socket_config_t socket_get_default_secure_server_config()
{
    socket_config_t config = { 0 };
    config.role = socket_role_server;
    config.tls.enable = true;
    config.local.port = DEFAULT_LISTENING_PORT;
    return config;
}

static inline socket_config_t socket_get_default_secure_client_config()
{
    socket_config_t config = { 0 };
    config.role = socket_role_client;
    config.tls.enable = true;
    config.local.port = 0;
    return config;
}

/* Plain (no-TLS) variants. Identical to the secure helpers above except
 * that tls.enable is false; useful for embedded testing or HTTP-only
 * deployments. */
static inline socket_config_t socket_get_default_plain_server_config()
{
    socket_config_t config = { 0 };
    config.role = socket_role_server;
    config.tls.enable = false;
    config.local.port = DEFAULT_LISTENING_PORT;
    return config;
}

static inline socket_config_t socket_get_default_plain_client_config()
{
    socket_config_t config = { 0 };
    config.role = socket_role_client;
    config.tls.enable = false;
    config.local.port = 0;
    return config;
}

result_t socket_init(socket_t* socket, socket_config_t* config);
result_t socket_deinit(socket_t* socket);

/* ------------------------------------------------------------------------- *
 * Blocking API.
 *
 * Intended for clients, simple utilities, and tests where multiplexing
 * many connections from a single thread is not required. Each call
 * runs to completion (or error) before returning. The blocking API is
 * also what the #stream_t adapter in `socket_stream.c` is built on, so
 * higher-level code that consumes a #stream_t (e.g. http-c's client-side
 * request/response path) implicitly uses these.
 * ------------------------------------------------------------------------- */
result_t socket_accept (socket_t* server, socket_t* client);
task_t*  socket_accept_async(socket_t* server, socket_t* client);
result_t socket_connect(socket_t* client);
result_t socket_read   (socket_t* socket, span_t buffer, span_t* out_read, span_t* remainder);
result_t socket_write  (socket_t* socket, span_t data);

/* ------------------------------------------------------------------------- *
 * Non-blocking API.
 *
 * Intended for event-loop driven servers (and clients) that multiplex
 * many file descriptors from a single thread. The caller drives
 * readiness via an external event loop and retries on `try_again`.
 *
 * After a successful `socket_init` for a server role, call
 * `socket_set_nonblocking(server->listen_sd)`. Then for each client:
 *   1. socket_accept_nb(server, &client)         -> try_again | ok | error
 *   2. socket_set_nonblocking(client.sd)
 *   3. loop: socket_handshake_nb(&client)        -> try_again | ok | error
 *   4. socket_read_nb / socket_write_nb          -> try_again | ok | error
 *
 * For a client role, after `socket_init`:
 *   1. socket_connect_nb_begin(&client)          -> ok (TCP in progress) | error
 *   2. loop: socket_connect_nb_continue(&client) -> try_again | ok | error
 *   3. loop: socket_handshake_nb(&client)        -> try_again | ok | error
 *
 * `socket_get_io_want` exposes whether the next operation needs the fd
 * to become readable, writable, or both, so the caller can re-arm the
 * event loop accordingly.
 * ------------------------------------------------------------------------- */

typedef enum socket_io_want
{
    socket_io_want_none  = 0,
    socket_io_want_read  = 1 << 0,
    socket_io_want_write = 1 << 1,
} socket_io_want_t;

result_t socket_set_nonblocking(int fd);

result_t socket_accept_nb(socket_t* server, socket_t* client);
result_t socket_handshake_nb(socket_t* socket);
uint32_t socket_get_io_want(socket_t* socket);

result_t socket_connect_nb_begin(socket_t* client);
result_t socket_connect_nb_continue(socket_t* client);

/**
 * @brief Non-blocking write. Writes as many bytes as possible and reports
 *        the number actually accepted in `*out_written`.
 *
 * @return ok (all written), try_again (would block - caller must retry
 *         later, possibly with a smaller / advanced span), or error.
 */
result_t socket_write_nb(socket_t* socket, span_t data, uint32_t* out_written);

/**
 * @brief Non-blocking read. Reads up to `cap` bytes from the connected
 *        socket into `dst` and stores the count actually received in
 *        `*out_received`. Transparently dispatches between the active
 *        TLS backend (when `tls.enabled`) and plain TCP (recv()).
 *
 * @return ok           Some bytes were read.
 *         try_again    No data ready; caller should re-arm and retry.
 *                      `*socket_get_io_want` reports the desired event mask.
 *         end_of_data  Peer closed the connection cleanly.
 *         error        Fatal I/O or TLS error.
 */
result_t socket_read_nb(socket_t* socket, void* dst, uint32_t cap,
                        uint32_t* out_received);

#endif // SOCKET_H
