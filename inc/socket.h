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

#endif // SOCKET_H
