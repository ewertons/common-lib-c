#ifndef SOCKET_H
#define SOCKET_H

#include <stdlib.h>
#include <stdbool.h>

#include "span.h"
#include "niceties.h"
#include "task.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <openssl/ssl.h>

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

    bool use_tls;
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
    SSL_CTX* ctx;
    SSL*     ssl;
    X509*    client_cert;
    char*    str;

    /* Non-blocking driver state. `io_want` is the bitmask of
     * #socket_io_want_t the last NB operation reported. `tcp_connected`
     * indicates the connect() phase finished for clients. */
    uint32_t io_want;
    bool     tcp_connected;
    bool     tls_handshake_done;

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

result_t socket_init(socket_t* socket, socket_config_t* config);
result_t socket_deinit(socket_t* socket);
result_t socket_accept(socket_t* server, socket_t* client);
task_t* socket_accept_async(socket_t* server, socket_t* client);
result_t socket_connect(socket_t* client);
result_t socket_read(socket_t* ssl1, span_t buffer, span_t* out_read, span_t* remainder);
result_t socket_write(socket_t* ssl1, span_t data);

/* ------------------------------------------------------------------------- *
 * Non-blocking primitives. The caller drives readiness via an external
 * event loop and retries on `try_again`.
 *
 * After a successful `socket_init` for a server role, call
 * `socket_set_nonblocking(server->listen_sd)`. Then for each client:
 *   1. socket_accept_nb(server, &client)         -> try_again | ok | error
 *   2. socket_set_nonblocking(client.sd)
 *   3. loop: socket_handshake_nb(&client)        -> try_again | ok | error
 *   4. socket_read / socket_write_nb             -> partial / try_again / ok
 *
 * For a client role, after `socket_init`:
 *   1. socket_connect_nb_begin(&client)          -> ok (TCP in progress) | error
 *   2. loop: socket_connect_nb_continue(&client) -> try_again | ok | error
 *   3. loop: socket_handshake_nb(&client)        -> try_again | ok | error
 *
 * `socket_get_handshake_wants` exposes whether the next operation needs
 * the fd to become readable, writable, or both, so the caller can re-arm
 * the event loop accordingly.
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

#endif // SOCKET_H
