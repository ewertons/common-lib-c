#ifndef SOCKET_H
#define SOCKET_H

#include <stdlib.h>
#include <stdbool.h>
#include "span.h"

#include <arpa/inet.h>

#include <sys/socket.h>
#include <openssl/ssl.h>

#define DEFAULT_LISTENING_PORT 8234

typedef struct socket_config
{
    bool use_tls;
    struct
    {
        int port;
    } local;
    struct
    {
        bool enable;
        const char* certificate_file;
        const char* private_key_file;
    } tls;
} socket_config_t;

typedef struct socket
{
  int listen_sd;
  int sd;
  struct sockaddr_in sa_serv;
  struct sockaddr_in sa_cli;
  socklen_t client_len;
  SSL_CTX* ctx;
  SSL*     ssl;
  X509*    client_cert;
  char*    str;
} socket_t;

static inline socket_config_t socket_get_default_secure_listener_config()
{
    socket_config_t config = { 0 };
    config.tls.enable = true;
    config.local.port = DEFAULT_LISTENING_PORT;
    return config;
}

int socket_init(socket_t* socket, socket_config_t* config);
int socket_accept(socket_t* server, socket_t* client);
int socket_read(socket_t* ssl1, span_t buffer, span_t* out_read);
int socket_write(socket_t* ssl1, span_t data);

#endif // SOCKET_H
