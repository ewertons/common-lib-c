/* GNU extensions: accept4(), pipe2(), splice(), MSG_ZEROCOPY,
 * SO_BINDTODEVICE, TCP_KEEPIDLE — declared only when _GNU_SOURCE is set
 * by the time <features.h> is pulled in via <stdlib.h>. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <socket.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/crypto.h>
#include <openssl/bio.h>
#include <openssl/x509v3.h>
#include <openssl/evp.h>
#include <openssl/sha.h>

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/uio.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <unistd.h>

#include "logging_simple.h"
#include "niceties.h"

#define SSL_DO_HANDSHAKE_SUCCESS 1

#ifndef SOCKET_LISTEN_BACKLOG
#define SOCKET_LISTEN_BACKLOG 128
#endif

/* ------------------------------------------------------------------------- *
 * OpenSSL backend.
 *
 * The opaque #tls_backend handle declared in socket.h is defined here so
 * that the OpenSSL types (SSL, SSL_CTX, X509) never leak into the public
 * surface. A future mbedtls backend would live in its own translation
 * unit and provide a different definition of the same struct -- the
 * library is built with exactly one TLS backend linked in.
 *
 * We allocate one of these on demand for any socket that has tls.enabled
 * set; plain-TCP sockets keep `tls.backend == NULL`.
 * ------------------------------------------------------------------------- */
struct tls_backend
{
    SSL_CTX* ctx;
    SSL*     ssl;

    /* §3.2 S3 — optional 32-byte SHA-256 of peer SPKI for pinning.
     * `pinned_spki_set == false` means no pinning. Copied to each
     * accepted client backend in socket_accept_nb so the verification
     * runs once the per-connection SSL has completed its handshake. */
    bool          pinned_spki_set;
    uint8_t       pinned_spki_sha256[32];

    /* §3.2 S1 — optional post-handshake user callback. */
    int   (*verify_peer_cb)(void* cert, void* userdata);
    void*   verify_peer_userdata;
};

static struct tls_backend* tls_backend_new(void)
{
    struct tls_backend* b = (struct tls_backend*)calloc(1, sizeof(*b));
    return b;
}

static void tls_backend_destroy(struct tls_backend** pb)
{
    if (pb == NULL || *pb == NULL) return;
    struct tls_backend* b = *pb;
    if (b->ssl != NULL)
    {
        (void)SSL_shutdown(b->ssl);
        SSL_free(b->ssl);
        b->ssl = NULL;
    }
    if (b->ctx != NULL)
    {
        SSL_CTX_free(b->ctx);
        b->ctx = NULL;
    }
    free(b);
    *pb = NULL;
}

static bool is_socket_library_initialized = false;

/* Reset all file descriptors to -1 so that an early socket_deinit does not
 * accidentally close fd 0/stdin. */
static void socket_clear_fds(socket_t* s)
{
    s->listen_sd = -1;
    s->sd        = -1;
}

static void socket_error(SSL *ssl, int err)
{
    int socket_err = SSL_get_error(ssl, err);

    log_error("SSL_accept() error code %d", socket_err);

    if (socket_err == SSL_ERROR_SYSCALL)
    {
        if (err == -1)
            log_error("  I/O error (%s)", strerror(errno));
        if (err == 0)
            log_error("  SSL peer closed connection");
    }
}

static int add_certificate_to_store(SSL_CTX* ssl_context, const char* certificate_file_path)
{
    int result = 0;

    if (certificate_file_path != NULL)
    {
        X509_STORE* cert_store = SSL_CTX_get_cert_store(ssl_context);

        if (cert_store == NULL)
        {
            printf("failure in SSL_CTX_get_cert_store.\n");
            result = 1;
        }
        else
        {
#if (OPENSSL_VERSION_NUMBER >= 0x10100000L) || defined(LIBRESSL_VERSION_NUMBER)
            const BIO_METHOD* bio_method;
#else
            BIO_METHOD* bio_method;
#endif
            bio_method = BIO_s_mem();

            if (bio_method == NULL)
            {
                printf("failure in BIO_s_mem\n");
                result = 1;
            }
            else
            {
                BIO* cert_file_bio = BIO_new_file(certificate_file_path, "r");

                if (cert_file_bio == NULL)
                {
                    printf("failure in BIO_file_new\n");
                    result = 1;
                }
                else
                {
                    {
                        {
                            X509* certificate;

                            while ((certificate = PEM_read_bio_X509(cert_file_bio, NULL, NULL, NULL)) != NULL)
                            {
                                if (!X509_STORE_add_cert(cert_store, certificate))
                                {
                                    X509_free(certificate);
                                    printf("failure in X509_STORE_add_cert\n");
                                    break;
                                }
                                X509_free(certificate);
                            }
                            if (certificate == NULL)
                            {
                                result = 0;/*all is fine*/
                            }
                            else
                            {
                                /*previous while loop terminated unfortunately*/
                                result = 1;
                            }
                        }
                    }

                    BIO_free(cert_file_bio);
                }
            }
        }
    }

    return result;
}

static const char* SSL_version_to_string(int version)
{
    switch(version)
    {
        case SSL3_VERSION:
            return "SSL3";
            break;
        case TLS1_VERSION:
            return "TLS/1.0";
            break;
        case TLS1_1_VERSION:
            return "TLS/1.1";
            break;
        case TLS1_2_VERSION:
            return "TLS/1.2";
            break;
        case TLS1_3_VERSION:
            return "TLS/1.3";
            break;
        case DTLS1_VERSION:
            return "DTLS/1.0";
            break;
        case DTLS1_2_VERSION:
            return "DTLS/1.2";
            break;
        case DTLS1_BAD_VER:
            return "DTLS1_BAD_VER";
            break;
        default:
            return "UNDEFINED";
    }
}

static const char* SSL_content_type_to_string(int content_type)
{
    switch(content_type)
    {
        case SSL3_RT_CHANGE_CIPHER_SPEC:
            return "change cipher";
            break;
        case SSL3_RT_ALERT:
            return "alert";
            break;
        case SSL3_RT_HANDSHAKE:
            return "handshake";
            break;
        case SSL3_RT_APPLICATION_DATA:
            return "data";
            break;
        case SSL3_RT_HEADER:
            return "header";
            break;
        case SSL3_RT_INNER_CONTENT_TYPE:
            return "inner content type";
            break;
        default:
            return "undefined";
    }
}

typedef enum {
    invalid = 0,
    change_cipher_spec = 20,
    alert = 21,
    handshake = 22,
    application_data = 23,
    reserved = 255
} ssl_content_type_t;

static const char* ssl_content_type_to_string(ssl_content_type_t content_type)
{
    switch(content_type)
    {
        case invalid:
            return "invalid";
            break;
        case change_cipher_spec:
            return "change_cipher_spec";
            break;
        case alert:
            return "alert";
            break;
        case handshake:
            return "handshake";
            break;
        case application_data:
            return "application_data";
            break;
        default:
            return "undefined";
            break;
    }
}

// https://tls12.xargs.org/#client-hello/annotated
typedef uint16_t tls_protocol_version_t;

typedef uint32_t uint24_t;

// https://www.rfc-editor.org/rfc/rfc8446#page-79
typedef struct
{
    ssl_content_type_t type;
    tls_protocol_version_t legacy_record_version;
    uint16_t length;
} ssl_header_t;

typedef enum
{
    tls_handshake_type_hello_request_RESERVED = (0),
    tls_handshake_type_client_hello = (1),
    tls_handshake_type_server_hello = (2),
    tls_handshake_type_hello_verify_request_RESERVED = (3),
    tls_handshake_type_new_session_ticket = (4),
    tls_handshake_type_end_of_early_data = (5),
    tls_handshake_type_hello_retry_request_RESERVED = (6),
    tls_handshake_type_encrypted_extensions = (8),
    tls_handshake_type_certificate = (11),
    tls_handshake_type_server_key_exchange_RESERVED = (12),
    tls_handshake_type_certificate_request = (13),
    tls_handshake_type_server_hello_done_RESERVED = (14),
    tls_handshake_type_certificate_verify = (15),
    tls_handshake_type_client_key_exchange_RESERVED = (16),
    tls_handshake_type_finished = (20),
    tls_handshake_type_certificate_url_RESERVED = (21),
    tls_handshake_type_certificate_status_RESERVED = (22),
    tls_handshake_type_supplemental_data_RESERVED = (23),
    tls_handshake_type_key_update = (24),
    tls_handshake_type_message_hash = (254),
    tls_handshake_type_undefined = (255)
} tls_handshake_type_t;

static const char* tls_handshake_type_to_string(tls_handshake_type_t type)
{
    switch(type)
    {
        case tls_handshake_type_hello_request_RESERVED:
            return "hello_request";
            break;
        case tls_handshake_type_client_hello:
            return "client_hello";
            break;
        case tls_handshake_type_server_hello:
            return "server_hello";
            break;
        case tls_handshake_type_hello_verify_request_RESERVED:
            return "hello_verify_request";
            break;
        case tls_handshake_type_new_session_ticket:
            return "new_session_ticket";
            break;
        case tls_handshake_type_end_of_early_data:
            return "end_of_early_data";
            break;
        case tls_handshake_type_hello_retry_request_RESERVED:
            return "hello_retry_request";
            break;
        case tls_handshake_type_encrypted_extensions:
            return "encrypted_extensions";
            break;
        case tls_handshake_type_certificate:
            return "certificate";
            break;
        case tls_handshake_type_server_key_exchange_RESERVED:
            return "server_key_exchange";
            break;
        case tls_handshake_type_certificate_request:
            return "certificate_request";
            break;
        case tls_handshake_type_server_hello_done_RESERVED:
            return "server_hello_done";
            break;
        case tls_handshake_type_certificate_verify:
            return "certificate_verify";
            break;
        case tls_handshake_type_client_key_exchange_RESERVED:
            return "client_key_exchange";
            break;
        case tls_handshake_type_finished:
            return "finished";
            break;
        case tls_handshake_type_certificate_url_RESERVED:
            return "certificate_url";
            break;
        case tls_handshake_type_certificate_status_RESERVED:
            return "certificate_status";
            break;
        case tls_handshake_type_supplemental_data_RESERVED:
            return "supplemental_data";
            break;
        case tls_handshake_type_key_update:
            return "key_update";
            break;
        case tls_handshake_type_message_hash:
            return "message_hash";
            break;
        case tls_handshake_type_undefined:
            return "undefined";
            break;
        default:
            return "undefined";
            break;
    }
}

typedef struct
{
    tls_protocol_version_t legacy_version;
    const uint8_t* random;
    const uint8_t* legacy_session_id;
    // opaque legacy_session_id<0..32>;
    // CipherSuite cipher_suites<2..2^16-2>;
    // opaque legacy_compression_methods<1..2^8-1>;
    // Extension extensions<8..2^16-1>;
} tls_protocol_client_hello_t;

// https://www.rfc-editor.org/rfc/rfc8446#page-124
typedef struct
{
    tls_handshake_type_t type;
    uint24_t length;
    union {
        tls_protocol_client_hello_t client_hello;
    } data;
} tls_handshake_t;

static void print_ssl_message(int content_type, const uint8_t *buffer, size_t length)
{
    if (content_type == SSL3_RT_HEADER)
    {
        ssl_header_t header;
        header.type =(ssl_content_type_t) buffer[0];
        header.legacy_record_version = (tls_protocol_version_t)((buffer[1] << 8) + buffer[2]);
        header.length = ((buffer[3] << 8) + buffer[4]);
        printf(" content-type=%s, version=%s, length=%d",
            ssl_content_type_to_string(header.type), SSL_version_to_string(header.legacy_record_version), header.length);
    }
    else if (content_type == SSL3_RT_HANDSHAKE)
    {
        tls_handshake_t handshake;
        handshake.type = (tls_handshake_type_t)buffer[0];
        handshake.length = ((buffer[1] << 16) + (buffer[2] << 8) + buffer[3]);

        printf(" %s (%u)", tls_handshake_type_to_string(handshake.type), handshake.length);

        if (handshake.type == tls_handshake_type_client_hello)
        {
            handshake.data.client_hello.legacy_version = (tls_protocol_version_t)((buffer[4] << 8) + buffer[5]);
            handshake.data.client_hello.random = &buffer[5];

            if (handshake.data.client_hello.legacy_version < TLS1_3_VERSION)
            {
                handshake.data.client_hello.legacy_session_id = &buffer[37];
            }
            else
            {
                printf(" no-random");
                handshake.data.client_hello.legacy_session_id = NULL;
            }

            printf(" %02x (%02x)", handshake.data.client_hello.legacy_version, TLS1_3_VERSION);
        }
    }
    else
    {
        for (int i = 0; i< length; i++)
        {
            printf(" %02x", buffer[i]);
        }
    }
}

static void SSL_debug(int write_p, int version,
                      int content_type, const void *buf,
                      size_t len, SSL *ssl, void *arg)
{
    (void)ssl;
    (void)arg;
    printf("%s [%s] %s (%d bytes):", write_p == 0 ? "<-" : "->",
        SSL_version_to_string(version), SSL_content_type_to_string(content_type), (int)len);
    print_ssl_message(content_type, (const uint8_t*)buf, len);
    printf("\n");
}

result_t socket_init(socket_t *s, socket_config_t *config)
{
    if (!is_socket_library_initialized)
    {
        SSL_load_error_strings();
        SSL_library_init();
        /* A peer disconnect during SSL_write/send must not kill the process. */
        (void)signal(SIGPIPE, SIG_IGN);
        is_socket_library_initialized = true;
    }

    int socket_result;
    const SSL_METHOD *ssl_method = (config->role == socket_role_server ? TLS_server_method() : TLS_client_method());

    (void)memset(s, 0, sizeof(socket_t));
    socket_clear_fds(s);

    s->role        = config->role;
    s->local       = config->local;
    s->remote      = config->remote;
    s->tls.enabled = config->tls.enable;

    if (s->tls.enabled)
    {
        s->tls.backend = tls_backend_new();
        if (s->tls.backend == NULL)
        {
            return error;
        }
        s->tls.backend->ctx = SSL_CTX_new(ssl_method);

        if (s->tls.backend->ctx == NULL)
        {
            return error;
        }

        if (config->tls.trusted_certificate_file != NULL &&
            SSL_CTX_load_verify_locations(s->tls.backend->ctx, config->tls.trusted_certificate_file, NULL) != 1)
        {
            log_error("SSL_CTX_load_verify_locations failed");
            return error;
        }

        if (config->tls.certificate_file != NULL &&
            SSL_CTX_use_certificate_file(s->tls.backend->ctx, config->tls.certificate_file, SSL_FILETYPE_PEM) <= 0)
        {
            log_error("SSL_CTX_use_certificate_file failed");
            return error;
        }

        if (config->tls.private_key_file != NULL)
        {
            if (SSL_CTX_use_PrivateKey_file(s->tls.backend->ctx, config->tls.private_key_file, SSL_FILETYPE_PEM) <= 0)
            {
                log_error("SSL_CTX_use_PrivateKey_file failed");
                return error;
            }

            if (!SSL_CTX_check_private_key(s->tls.backend->ctx))
            {
                log_error("Private key does not match the certificate public key");
                return error;
            }
        }

        /* §3.2 S2 — TLS 1.3 only / cipher suites / curves. */
        if (config->tls.tls13_only)
        {
            if (SSL_CTX_set_min_proto_version(s->tls.backend->ctx, TLS1_3_VERSION) != 1)
            {
                log_error("SSL_CTX_set_min_proto_version(TLS1_3) failed");
                return error;
            }
        }
        if (config->tls.cipher_suites != NULL &&
            SSL_CTX_set_ciphersuites(s->tls.backend->ctx, config->tls.cipher_suites) != 1)
        {
            log_error("SSL_CTX_set_ciphersuites failed");
            return error;
        }
        if (config->tls.curves != NULL &&
            SSL_CTX_set1_curves_list(s->tls.backend->ctx, config->tls.curves) != 1)
        {
            log_error("SSL_CTX_set1_curves_list failed");
            return error;
        }

        /* §3.2 S1 — enforce mTLS when require_peer_cert is set. */
        if (config->tls.require_peer_cert)
        {
            SSL_CTX_set_verify(s->tls.backend->ctx,
                               SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                               NULL);
        }

        /* §3.2 S3 — copy the pinned SPKI hash and callback into the
         * backend so post-handshake verification can find them. */
        if (config->tls.pinned_spki_sha256 != NULL)
        {
            memcpy(s->tls.backend->pinned_spki_sha256,
                   config->tls.pinned_spki_sha256, 32);
            s->tls.backend->pinned_spki_set = true;
        }
        s->tls.backend->verify_peer_cb       = config->tls.verify_peer_cb;
        s->tls.backend->verify_peer_userdata = config->tls.verify_peer_userdata;
    }

    if (config->role == socket_role_server)
    {
#ifdef SOCK_CLOEXEC
        s->listen_sd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
#else
        s->listen_sd = socket(AF_INET, SOCK_STREAM, 0);
#endif

        if (s->listen_sd == -1)
        {
            return error;
        }

        /* Allow rapid restarts of the listener. */
        int reuse = 1;
        (void)setsockopt(s->listen_sd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
        /* §3.1 P4 — only enable SO_REUSEPORT when the caller asked for it. */
        if (config->local.reuse_port)
        {
            (void)setsockopt(s->listen_sd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
        }
#endif
#ifdef SO_BINDTODEVICE
        /* §3.2 S4 — bind to a specific NIC if requested. */
        if (config->local.interface_name != NULL)
        {
            if (setsockopt(s->listen_sd, SOL_SOCKET, SO_BINDTODEVICE,
                           config->local.interface_name,
                           (socklen_t)strlen(config->local.interface_name)) != 0)
            {
                log_error("SO_BINDTODEVICE(%s) failed (%s)",
                          config->local.interface_name, strerror(errno));
                /* Non-fatal on platforms where this requires CAP_NET_RAW. */
            }
        }
#endif

        /* §3.1 P3 — TCP tuning knobs (zero-init = OS default). */
        if (config->tcp.nodelay)
        {
            int one = 1;
            (void)setsockopt(s->listen_sd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        }
#ifdef TCP_QUICKACK
        if (config->tcp.quickack)
        {
            int one = 1;
            (void)setsockopt(s->listen_sd, IPPROTO_TCP, TCP_QUICKACK, &one, sizeof(one));
        }
#endif
        if (config->tcp.send_buf_size > 0)
        {
            (void)setsockopt(s->listen_sd, SOL_SOCKET, SO_SNDBUF,
                             &config->tcp.send_buf_size,
                             sizeof(config->tcp.send_buf_size));
        }
        if (config->tcp.recv_buf_size > 0)
        {
            (void)setsockopt(s->listen_sd, SOL_SOCKET, SO_RCVBUF,
                             &config->tcp.recv_buf_size,
                             sizeof(config->tcp.recv_buf_size));
        }
        if (config->tcp.keepalive_idle_sec > 0)
        {
            int one = 1;
            (void)setsockopt(s->listen_sd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
#ifdef TCP_KEEPIDLE
            (void)setsockopt(s->listen_sd, IPPROTO_TCP, TCP_KEEPIDLE,
                             &config->tcp.keepalive_idle_sec,
                             sizeof(config->tcp.keepalive_idle_sec));
#endif
        }

        memset(&s->sa_serv, '\0', sizeof(s->sa_serv));
        s->sa_serv.sin_family = AF_INET;
        s->sa_serv.sin_addr.s_addr = INADDR_ANY;
        s->sa_serv.sin_port = htons(config->local.port); /* Server Port number */

        socket_result = bind(s->listen_sd, (struct sockaddr *)&s->sa_serv, sizeof(s->sa_serv));

        if (socket_result == -1)
        {
            log_error("bind to port %d (%s)", config->local.port, strerror(errno));
            return error;
        }

        socket_result = listen(s->listen_sd, SOCKET_LISTEN_BACKLOG);

        if (socket_result == -1)
        {
            log_error("listen() (%s)", strerror(errno));
            return error;
        }
        else
        {
            return ok;
        }
    }
    else
    {
        return ok;
    }
}

result_t socket_deinit(socket_t *socket)
{
    result_t result;

    if (socket == NULL)
    {
        result = invalid_argument;
    }
    else
    {
        tls_backend_destroy(&socket->tls.backend);

        if (socket->listen_sd != -1)
        {
            close(socket->listen_sd);
            socket->listen_sd = -1;
        }

        if (socket->sd != -1)
        {
            close(socket->sd);
            socket->sd = -1;
        }

        result = ok;
    }

    return result;
}

static void check_peer_certificates(SSL* ssl, const char* peer_name)
{
#if (OPENSSL_VERSION_NUMBER >= 0x30000000L)
    X509* client_cert = SSL_get1_peer_certificate(ssl);;
#else
    X509* client_cert = SSL_get_peer_certificate(ssl);;
#endif
    if (client_cert != NULL)
    {
        printf("%s certificate:\n", peer_name);

        char* str = X509_NAME_oneline(X509_get_subject_name(client_cert), 0, 0);
        if (!str)
        {
            printf("no subject\n");
            return;
        }

        printf("\t subject: %s\n", str);
        OPENSSL_free(str);

        str = X509_NAME_oneline(X509_get_issuer_name(client_cert), 0, 0);
        if (!str)
        {
            printf("no issuer name\n");
            return;
        }

        printf("\t issuer: %s\n", str);
        OPENSSL_free(str);

        /* We could do all sorts of certificate verification stuff here before
            deallocating the certificate. */

        X509_free(client_cert);
    }
    else
    {
        printf("%s does not have certificate.\n", peer_name);
    }
}

result_t socket_accept(socket_t *server, socket_t *client)
{
    result_t result = ok;
    int err;

    memset(client, 0, sizeof(socket_t));
    socket_clear_fds(client);
    client->client_len = sizeof(client->sa_cli);
    client->sd = accept(server->listen_sd, (struct sockaddr *)&client->sa_cli, &client->client_len);

    if (client->sd == -1)
    {
        log_error("accept() (%s)", strerror(errno));
        return error;
    }

    char buffer[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client->sa_cli.sin_addr.s_addr, buffer, sizeof(buffer));
    log_info("Connection from %s:%d", buffer, ntohs(client->sa_cli.sin_port));

    /* Inherit TLS-or-not from the listening socket. */
    client->tls.enabled = server->tls.enabled;

    if (!client->tls.enabled)
    {
        /* Plain TCP -- nothing more to do. */
        return ok;
    }

    /* ----------------------------------------------- */
    /* TCP connection is ready. Do server side SSL. */

    client->tls.backend = tls_backend_new();
    if (client->tls.backend == NULL)
    {
        close(client->sd);
        client->sd = -1;
        return error;
    }
    client->tls.backend->ssl = SSL_new(server->tls.backend->ctx);

    SSL_set_fd(client->tls.backend->ssl, client->sd);

    err = SSL_accept(client->tls.backend->ssl);

    if (err != 1)
    {
        socket_error(client->tls.backend->ssl, err);
        log_error("SSL_accept (%s)", strerror(errno));
        tls_backend_destroy(&client->tls.backend);
        close(client->sd);
        client->sd = -1;
        result = error;
    }
    else
    {
        log_info("SSL connection using %s", SSL_get_cipher(client->tls.backend->ssl));

        /* Get client's certificate */

        check_peer_certificates(client->tls.backend->ssl, "client");

        result = ok;
    }

    return result;
}

static result_t internal_socket_accept_async(void *user_args, task_t *my_task)
{
    socket_t *client = (socket_t *)user_args;

    if (task_is_cancelled(my_task))
    {
        return cancelled;
    }
    else
    {
        return socket_accept(client->parent, client);
    }
}

task_t *socket_accept_async(socket_t *server, socket_t *client)
{
    client->parent = server;
    return task_run(internal_socket_accept_async, client);
}

// https://cpp.hotexamples.com/examples/-/-/SSL_set_fd/cpp-ssl_set_fd-function-examples.html
result_t socket_connect(socket_t *client)
{
    result_t result;

    if (client == NULL || client->role == socket_role_server)
    {
        result = invalid_argument;
    }
    else
    {
        /* ----------------------------------------------- */
        /* TCP connection is ready. Do server side SSL. */
        int err, rv;
        int sockfd, numbytes;
        struct addrinfo hints, *servinfo, *p;

        (void)memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        uint8_t port_string[6] = {0};

        if (span_is_empty(span_copy_int32(span_from_memory(port_string), client->remote.port, NULL)))
        {
            log_error("span_copy_int32 failed");
            return error;
        }

        if ((rv = getaddrinfo(span_get_ptr(client->remote.hostname), port_string, &hints, &servinfo)) != 0)
        {
            log_error("getaddrinfo: %s", gai_strerror(rv));
            return error;
        }

        for (p = servinfo; p != NULL; p = p->ai_next)
        {
            if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
            {
                log_error("client: socket (%s)", strerror(errno));
                continue;
            }

            if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1)
            {
                close(sockfd);
                continue;
            }

            break;
        }

        freeaddrinfo(servinfo);

        if (p == NULL)
        {
            log_error("client: failed to connect");
            result = error;
        }
        else
        {
            client->sd = sockfd;

            if (!client->tls.enabled)
            {
                /* Plain TCP -- skip the entire SSL setup below. */
                result = ok;
            }
            else
            {
            /* socket_init already allocated the TLS backend and ctx for
             * this client; we just attach a new SSL session to that ctx. */
            client->tls.backend->ssl = SSL_new(client->tls.backend->ctx);

            SSL_set_verify(client->tls.backend->ssl, SSL_VERIFY_PEER, NULL);

            /* Bind hostname verification to the configured remote.hostname
             * so the server cert's SAN/CN must match the name we asked to
             * connect to. Without this, the chain is verified but any cert
             * issued by a trusted CA would be accepted regardless of
             * subject. */
            if (!span_is_empty(client->remote.hostname))
            {
                X509_VERIFY_PARAM* vp = SSL_get0_param(client->tls.backend->ssl);
                X509_VERIFY_PARAM_set_hostflags(vp,
                    X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
                if (X509_VERIFY_PARAM_set1_host(vp,
                        (const char*)span_get_ptr(client->remote.hostname),
                        0) != 1)
                {
                    log_error("X509_VERIFY_PARAM_set1_host failed");
                }
            }

            err = SSL_set_fd(client->tls.backend->ssl, client->sd);

            if (err != 1)
            {
                socket_error(client->tls.backend->ssl, err);
                SSL_free(client->tls.backend->ssl);
                client->tls.backend->ssl = NULL;
                close(client->sd);
                client->sd = -1;
                result = error;
            }
            else
            {
                ERR_clear_error();
                err = SSL_connect(client->tls.backend->ssl);

                if (err != SSL_DO_HANDSHAKE_SUCCESS)
                {
                    int ssl_err = SSL_get_error(client->tls.backend->ssl, err);

                    if (ssl_err == SSL_ERROR_SSL)
                    {
                        log_error("SSL_connect: %s", ERR_error_string(ERR_get_error(), NULL));
                    }
                    else
                    {
                        log_error("SSL handshake failed: %d", ssl_err);
                    }

                    SSL_free(client->tls.backend->ssl);
                    client->tls.backend->ssl = NULL;
                    close(client->sd);
                    client->sd = -1;
                    result = error;
                }
                else
                {
                    log_info("SSL_get_verify_result=%ld", SSL_get_verify_result(client->tls.backend->ssl));
                    check_peer_certificates(client->tls.backend->ssl, "server");
                    result = ok;
                }
            }
            } /* end of TLS-enabled branch */
        }
    }

    return result;
}

result_t socket_read(socket_t *s, span_t buffer, span_t *out_read, span_t* remainder)
{
    result_t result;

    if (s == NULL || out_read == NULL)
    {
        result = invalid_argument;
    }
    else if (!s->tls.enabled)
    {
        /* Plain TCP read. recv() returns 0 on orderly shutdown. */
        ssize_t bytes_read = recv(s->sd, span_get_ptr(buffer),
                                  span_get_size(buffer), 0);
        if (bytes_read > 0)
        {
            *out_read = span_slice(buffer, 0, (uint32_t)bytes_read);
            if (remainder != NULL)
            {
                *remainder = span_slice_to_end(buffer, (uint32_t)bytes_read);
            }
            result = ok;
        }
        else if (bytes_read == 0)
        {
            result = end_of_data;
        }
        else if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        {
            if (remainder != NULL)
            {
                *remainder = buffer;
            }
            result = try_again;
        }
        else
        {
            result = error;
        }
    }
    else
    {
        int bytes_read;

        bytes_read = SSL_read(s->tls.backend->ssl, span_get_ptr(buffer), span_get_size(buffer));

        if (bytes_read > 0)
        {
            *out_read = span_slice(buffer, 0, bytes_read);

            if (remainder != NULL)
            {
                *remainder = span_slice_to_end(buffer, bytes_read);
            }

            result = ok;
        }
        else
        {
            int reason = SSL_get_error(s->tls.backend->ssl, bytes_read);

            switch (reason)
            {
            case SSL_ERROR_ZERO_RETURN:
                /* Peer closed cleanly. */
                result = end_of_data;
                break;
            case SSL_ERROR_SYSCALL:
            case SSL_ERROR_SSL:
                result = error;
                break;
            default:
                /* SSL_ERROR_WANT_READ / WANT_WRITE - retryable. */
                if (remainder != NULL)
                {
                    *remainder = buffer;
                }
                result = try_again;
                break;
            };
        }
    }

    return result;
}

result_t socket_write(socket_t *s, span_t data)
{
    result_t result;

    if (s == NULL)
    {
        result = invalid_argument;
    }
    else if (!s->tls.enabled)
    {
        result = ok;
        while (span_get_size(data) > 0)
        {
            ssize_t n = send(s->sd, span_get_ptr(data),
                             span_get_size(data), 0);
            if (n <= 0)
            {
                if (n < 0 && errno == EINTR) continue;
                result = error;
                log_error("send (%s)", strerror(errno));
                break;
            }
            data = span_slice_to_end(data, (uint32_t)n);
        }
    }
    else
    {
        result = ok;

        while (span_get_size(data) > 0)
        {
            int n = SSL_write(s->tls.backend->ssl, span_get_ptr(data), span_get_size(data));

            if (n <= 0)
            {
                result = error;
                log_error("SSL_write (ssl_err=%d errno=%s)", SSL_get_error(s->tls.backend->ssl, n), strerror(errno));
                break;
            }
            else
            {
                data = span_slice_to_end(data, n);
            }
        }
    }

    return result;
}

/* ------------------------------------------------------------------------- *
 *                          Non-blocking primitives
 * ------------------------------------------------------------------------- */

result_t socket_set_nonblocking(int fd)
{
    if (fd < 0)
    {
        return invalid_argument;
    }
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
    {
        return error;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        return error;
    }
    return ok;
}

uint32_t socket_get_io_want(socket_t* s)
{
    return (s == NULL) ? 0 : s->io_want;
}

result_t socket_accept_nb(socket_t* server, socket_t* client)
{
    if (server == NULL || client == NULL)
    {
        return invalid_argument;
    }

    memset(client, 0, sizeof(socket_t));
    socket_clear_fds(client);
    client->client_len = sizeof(client->sa_cli);

    /* §3.1 P5 — accept4() returns a non-blocking, close-on-exec fd in one
     * syscall, replacing accept()+fcntl(O_NONBLOCK)+FD_CLOEXEC. */
    int sd = accept4(server->listen_sd,
                     (struct sockaddr*)&client->sa_cli, &client->client_len,
                     SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (sd == -1)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return try_again;
        }
        if (errno == EINTR)
        {
            return try_again;
        }
        log_error("accept_nb (%s)", strerror(errno));
        return error;
    }

    client->sd  = sd;
    client->role = socket_role_server; /* server-side endpoint of an accepted connection */
    client->tls.enabled = server->tls.enabled;

    /* accept4() already set O_NONBLOCK and FD_CLOEXEC -- no follow-up
     * socket_set_nonblocking() call needed here. */

    if (!client->tls.enabled)
    {
        /* Plain TCP -- no handshake required. */
        client->tcp_connected      = true;
        client->tls.handshake_done = true;
        client->io_want            = socket_io_want_read;
        return ok;
    }

    /* Prime the TLS handshake. The caller must drive socket_handshake_nb
     * until it returns ok. */
    client->tls.backend = tls_backend_new();
    if (client->tls.backend == NULL)
    {
        close(sd);
        client->sd = -1;
        return error;
    }
    client->tls.backend->ssl = SSL_new(server->tls.backend->ctx);
    if (client->tls.backend->ssl == NULL)
    {
        tls_backend_destroy(&client->tls.backend);
        close(sd);
        client->sd = -1;
        return error;
    }
    SSL_set_fd(client->tls.backend->ssl, sd);
    SSL_set_accept_state(client->tls.backend->ssl);

    /* Inherit pin / verify-cb from the listening server's backend so the
     * post-handshake hook in socket_handshake_nb can find them. */
    client->tls.backend->pinned_spki_set      = server->tls.backend->pinned_spki_set;
    if (server->tls.backend->pinned_spki_set)
    {
        memcpy(client->tls.backend->pinned_spki_sha256,
               server->tls.backend->pinned_spki_sha256, 32);
    }
    client->tls.backend->verify_peer_cb       = server->tls.backend->verify_peer_cb;
    client->tls.backend->verify_peer_userdata = server->tls.backend->verify_peer_userdata;

    client->tcp_connected      = true;
    client->tls.handshake_done = false;
    client->io_want            = socket_io_want_read;

    return ok;
}

result_t socket_handshake_nb(socket_t* s)
{
    if (s == NULL)
    {
        return invalid_argument;
    }
    if (!s->tls.enabled)
    {
        /* Nothing to do for plain TCP -- TCP is already connected by the
         * time we get here. */
        s->tls.handshake_done = true;
        s->io_want            = 0;
        return ok;
    }
    if (s->tls.backend == NULL || s->tls.backend->ssl == NULL)
    {
        return invalid_argument;
    }
    if (s->tls.handshake_done)
    {
        s->io_want = 0;
        return ok;
    }

    ERR_clear_error();
    int rc = SSL_do_handshake(s->tls.backend->ssl);
    if (rc == 1)
    {
        /* §3.2 S3 — SPKI pin verification. */
        if (s->tls.backend->pinned_spki_set)
        {
            X509* peer = SSL_get_peer_certificate(s->tls.backend->ssl);
            if (peer == NULL)
            {
                log_error("SPKI pin: no peer certificate");
                return error;
            }
            uint8_t* spki_der = NULL;
            int spki_len = i2d_X509_PUBKEY(X509_get_X509_PUBKEY(peer), &spki_der);
            X509_free(peer);
            if (spki_len <= 0 || spki_der == NULL)
            {
                log_error("SPKI pin: i2d_X509_PUBKEY failed");
                if (spki_der != NULL) OPENSSL_free(spki_der);
                return error;
            }
            uint8_t hash[32];
            unsigned int hlen = 0;
            int ok_evp = EVP_Digest(spki_der, (size_t)spki_len, hash, &hlen,
                                    EVP_sha256(), NULL);
            OPENSSL_free(spki_der);
            if (ok_evp != 1 || hlen != 32 ||
                memcmp(hash, s->tls.backend->pinned_spki_sha256, 32) != 0)
            {
                log_error("SPKI pin mismatch");
                return error;
            }
        }

        /* §3.2 S1 — optional user callback. */
        if (s->tls.backend->verify_peer_cb != NULL)
        {
            X509* peer = SSL_get_peer_certificate(s->tls.backend->ssl);
            int cb_rc = s->tls.backend->verify_peer_cb(
                            (void*)peer, s->tls.backend->verify_peer_userdata);
            if (peer != NULL) X509_free(peer);
            if (cb_rc != 0)
            {
                log_error("verify_peer_cb rejected peer (rc=%d)", cb_rc);
                return error;
            }
        }

        s->tls.handshake_done = true;
        s->io_want            = 0;
        return ok;
    }

    int sslerr = SSL_get_error(s->tls.backend->ssl, rc);
    switch (sslerr)
    {
        case SSL_ERROR_WANT_READ:
            s->io_want = socket_io_want_read;
            return try_again;
        case SSL_ERROR_WANT_WRITE:
            s->io_want = socket_io_want_write;
            return try_again;
        default:
            log_error("SSL handshake failed: ssl_err=%d errno=%s",
                      sslerr, strerror(errno));
            return error;
    }
}

result_t socket_connect_nb_begin(socket_t* client)
{
    if (client == NULL || client->role == socket_role_server)
    {
        return invalid_argument;
    }

    int rv;
    struct addrinfo hints, *servinfo, *p;
    (void)memset(&hints, 0, sizeof hints);
    /* The rest of this socket layer is IPv4-only (sa_serv is a
     * sockaddr_in, bind/accept use AF_INET). Constrain the client
     * resolver to AF_INET so we never pick an IPv6 address that the
     * server side could not have bound to -- otherwise a non-blocking
     * connect to ::1 can return EINPROGRESS and silently never complete
     * because no IPv6 listener exists. */
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    uint8_t port_string[6] = { 0 };
    if (span_is_empty(span_copy_int32(span_from_memory(port_string),
                                      client->remote.port, NULL)))
    {
        return error;
    }

    if ((rv = getaddrinfo(span_get_ptr(client->remote.hostname),
                          (char*)port_string, &hints, &servinfo)) != 0)
    {
        log_error("getaddrinfo: %s", gai_strerror(rv));
        return error;
    }

    int sockfd = -1;
    bool in_progress = false;
    for (p = servinfo; p != NULL; p = p->ai_next)
    {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) continue;

        if (socket_set_nonblocking(sockfd) != ok)
        {
            close(sockfd);
            sockfd = -1;
            continue;
        }

        int cr = connect(sockfd, p->ai_addr, p->ai_addrlen);
        if (cr == 0)
        {
            client->tcp_connected = true;
            break;
        }
        if (errno == EINPROGRESS)
        {
            client->tcp_connected = false;
            in_progress = true;
            break;
        }
        close(sockfd);
        sockfd = -1;
    }

    freeaddrinfo(servinfo);

    if (sockfd == -1)
    {
        return error;
    }

    client->sd      = sockfd;
    client->io_want = in_progress ? socket_io_want_write : socket_io_want_none;
    return ok;
}

result_t socket_connect_nb_continue(socket_t* client)
{
    if (client == NULL || client->sd == -1)
    {
        return invalid_argument;
    }

    if (!client->tcp_connected)
    {
        int err = 0;
        socklen_t len = sizeof(err);
        if (getsockopt(client->sd, SOL_SOCKET, SO_ERROR, &err, &len) == -1)
        {
            return error;
        }
        if (err == EINPROGRESS || err == EALREADY)
        {
            client->io_want = socket_io_want_write;
            return try_again;
        }
        if (err != 0)
        {
            log_error("connect_nb (%s)", strerror(err));
            return error;
        }
        client->tcp_connected = true;
    }

    /* Initialize TLS state on first call after TCP is up. */
    if (!client->tls.enabled)
    {
        client->tls.handshake_done = true;
        client->io_want            = 0;
        return ok;
    }
    if (client->tls.backend == NULL)
    {
        return error;
    }
    if (client->tls.backend->ssl == NULL)
    {
        client->tls.backend->ssl = SSL_new(client->tls.backend->ctx);
        if (client->tls.backend->ssl == NULL)
        {
            return error;
        }
        SSL_set_fd(client->tls.backend->ssl, client->sd);
        SSL_set_connect_state(client->tls.backend->ssl);
        SSL_set_verify(client->tls.backend->ssl, SSL_VERIFY_PEER, NULL);
        if (!span_is_empty(client->remote.hostname))
        {
            X509_VERIFY_PARAM* vp = SSL_get0_param(client->tls.backend->ssl);
            X509_VERIFY_PARAM_set_hostflags(vp,
                X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
            if (X509_VERIFY_PARAM_set1_host(vp,
                    (const char*)span_get_ptr(client->remote.hostname),
                    0) != 1)
            {
                log_error("X509_VERIFY_PARAM_set1_host failed");
            }
        }
        client->io_want = socket_io_want_read;
    }
    return ok;
}

result_t socket_write_nb(socket_t* s, span_t data, uint32_t* out_written)
{
    if (s == NULL || out_written == NULL)
    {
        return invalid_argument;
    }
    *out_written = 0;

    if (span_is_empty(data))
    {
        s->io_want = 0;
        return ok;
    }

    if (!s->tls.enabled)
    {
        ssize_t n = send(s->sd, span_get_ptr(data),
                         span_get_size(data), 0);
        if (n > 0)
        {
            *out_written = (uint32_t)n;
            if ((uint32_t)n == span_get_size(data))
            {
                s->io_want = 0;
                return ok;
            }
            s->io_want = socket_io_want_write;
            return try_again;
        }
        if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        {
            s->io_want = socket_io_want_write;
            return try_again;
        }
        log_error("send_nb (%s)", strerror(errno));
        return error;
    }

    ERR_clear_error();
    int n = SSL_write(s->tls.backend->ssl, span_get_ptr(data), (int)span_get_size(data));
    if (n > 0)
    {
        *out_written = (uint32_t)n;
        if ((uint32_t)n == span_get_size(data))
        {
            s->io_want = 0;
            return ok;
        }
        /* Partial write — caller advances data by n and retries. */
        s->io_want = socket_io_want_write;
        return try_again;
    }

    int sslerr = SSL_get_error(s->tls.backend->ssl, n);
    switch (sslerr)
    {
        case SSL_ERROR_WANT_READ:
            s->io_want = socket_io_want_read;
            return try_again;
        case SSL_ERROR_WANT_WRITE:
            s->io_want = socket_io_want_write;
            return try_again;
        default:
            log_error("SSL_write_nb (ssl_err=%d errno=%s)",
                      sslerr, strerror(errno));
            return error;
    }
}

result_t socket_read_nb(socket_t* s, span_t dst, uint32_t* out_received)
{
    if (s == NULL || out_received == NULL)
    {
        return invalid_argument;
    }
    *out_received = 0;

    void*    dst_ptr = span_get_ptr(dst);
    uint32_t cap     = span_get_size(dst);

    if (cap == 0)
    {
        return ok;
    }
    if (dst_ptr == NULL)
    {
        return invalid_argument;
    }

    if (!s->tls.enabled)
    {
        ssize_t r = recv(s->sd, dst_ptr, (size_t)cap, 0);
        if (r > 0)
        {
            *out_received = (uint32_t)r;
            s->io_want    = 0;
            return ok;
        }
        if (r == 0)
        {
            return end_of_data;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        {
            s->io_want = socket_io_want_read;
            return try_again;
        }
        log_error("recv_nb (%s)", strerror(errno));
        return error;
    }

    if (s->tls.backend == NULL || s->tls.backend->ssl == NULL)
    {
        return invalid_argument;
    }

    ERR_clear_error();
    int n = SSL_read(s->tls.backend->ssl, dst_ptr, (int)cap);
    if (n > 0)
    {
        *out_received = (uint32_t)n;
        s->io_want    = 0;
        return ok;
    }
    int sslerr = SSL_get_error(s->tls.backend->ssl, n);
    switch (sslerr)
    {
        case SSL_ERROR_WANT_READ:
            s->io_want = socket_io_want_read;
            return try_again;
        case SSL_ERROR_WANT_WRITE:
            s->io_want = socket_io_want_write;
            return try_again;
        case SSL_ERROR_ZERO_RETURN:
            return end_of_data;
        default:
            log_error("SSL_read_nb (ssl_err=%d errno=%s)",
                      sslerr, strerror(errno));
            return error;
    }
}

/* ------------------------------------------------------------------------- *
 * §3.1 P2 — vectored non-blocking write.
 *
 * Plain TCP path: build an iovec array and call sendmsg(). When iov_cnt == 1
 * the implementation attempts MSG_ZEROCOPY (Linux >= 4.14) and silently
 * falls back to plain sendmsg() on ENOTSUP.  The TLS path serialises the
 * iovec into back-to-back SSL_write calls.
 * ------------------------------------------------------------------------- */
result_t socket_writev_nb(socket_t* s,
                          const span_t* iov, size_t iov_cnt,
                          uint32_t* out_written)
{
    if (s == NULL || out_written == NULL || (iov == NULL && iov_cnt > 0))
    {
        return invalid_argument;
    }
    *out_written = 0;
    if (iov_cnt == 0)
    {
        s->io_want = 0;
        return ok;
    }

    if (!s->tls.enabled)
    {
        struct iovec sysv[16];
        if (iov_cnt > sizeof(sysv) / sizeof(sysv[0]))
        {
            iov_cnt = sizeof(sysv) / sizeof(sysv[0]);
        }
        size_t total = 0;
        for (size_t i = 0; i < iov_cnt; ++i)
        {
            sysv[i].iov_base = span_get_ptr(iov[i]);
            sysv[i].iov_len  = span_get_size(iov[i]);
            total += sysv[i].iov_len;
        }

        struct msghdr mh = { 0 };
        mh.msg_iov    = sysv;
        mh.msg_iovlen = iov_cnt;

        int flags = MSG_NOSIGNAL;
#ifdef MSG_ZEROCOPY
        if (iov_cnt == 1)
        {
            ssize_t n = sendmsg(s->sd, &mh, flags | MSG_ZEROCOPY);
            if (n >= 0)
            {
                *out_written = (uint32_t)n;
                s->io_want = ((size_t)n == total) ? 0 : socket_io_want_write;
                return ((size_t)n == total) ? ok : try_again;
            }
            if (errno != ENOTSUP && errno != EOPNOTSUPP)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                {
                    s->io_want = socket_io_want_write;
                    return try_again;
                }
                log_error("sendmsg(MSG_ZEROCOPY) (%s)", strerror(errno));
                return error;
            }
            /* Fallthrough to plain sendmsg() on ENOTSUP. */
        }
#endif
        ssize_t n = sendmsg(s->sd, &mh, flags);
        if (n >= 0)
        {
            *out_written = (uint32_t)n;
            s->io_want = ((size_t)n == total) ? 0 : socket_io_want_write;
            return ((size_t)n == total) ? ok : try_again;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        {
            s->io_want = socket_io_want_write;
            return try_again;
        }
        log_error("sendmsg (%s)", strerror(errno));
        return error;
    }

    /* TLS path: SSL has no native scatter-gather; serialise. */
    if (s->tls.backend == NULL || s->tls.backend->ssl == NULL)
    {
        return invalid_argument;
    }
    for (size_t i = 0; i < iov_cnt; ++i)
    {
        uint32_t w = 0;
        result_t r = socket_write_nb(s, iov[i], &w);
        *out_written += w;
        if (r != ok)
        {
            return r;
        }
    }
    s->io_want = 0;
    return ok;
}

/* ------------------------------------------------------------------------- *
 * §3.1 P2 / §3.2 — zero-copy splice between two plain TCP sockets.
 *
 * Refuses with `result_splice_unsupported` if either side has TLS.  The
 * pipe used as kernel intermediary is created and torn down per call;
 * §8 of the improvement plan suggests caching it in `socket_t`, which is
 * left as a follow-up since the public §5 socket_t shape does not yet
 * carry that field.
 * ------------------------------------------------------------------------- */
result_t socket_splice(socket_t* src, socket_t* dst,
                       size_t max_bytes, size_t* out_spliced)
{
    if (src == NULL || dst == NULL || out_spliced == NULL)
    {
        return invalid_argument;
    }
    *out_spliced = 0;
    if (src->tls.enabled || dst->tls.enabled)
    {
        return result_splice_unsupported;
    }
    if (max_bytes == 0)
    {
        return ok;
    }

#if defined(__linux__)
    int pipefd[2];
    if (pipe2(pipefd, O_NONBLOCK | O_CLOEXEC) != 0)
    {
        log_error("pipe2 (%s)", strerror(errno));
        return error;
    }

    ssize_t in = splice(src->sd, NULL, pipefd[1], NULL, max_bytes,
                        SPLICE_F_NONBLOCK | SPLICE_F_MOVE);
    if (in == 0)
    {
        close(pipefd[0]); close(pipefd[1]);
        return end_of_data;
    }
    if (in < 0)
    {
        int e = errno;
        close(pipefd[0]); close(pipefd[1]);
        if (e == EAGAIN || e == EWOULDBLOCK || e == EINTR)
        {
            return try_again;
        }
        log_error("splice(in) (%s)", strerror(e));
        return error;
    }

    size_t written = 0;
    while (written < (size_t)in)
    {
        ssize_t out = splice(pipefd[0], NULL, dst->sd, NULL,
                             (size_t)in - written,
                             SPLICE_F_NONBLOCK | SPLICE_F_MOVE);
        if (out > 0)
        {
            written += (size_t)out;
            continue;
        }
        if (out < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        {
            break;
        }
        int e = errno;
        close(pipefd[0]); close(pipefd[1]);
        log_error("splice(out) (%s)", strerror(e));
        return error;
    }

    close(pipefd[0]); close(pipefd[1]);
    *out_spliced = written;
    return (written == (size_t)in) ? ok : try_again;
#else
    (void)max_bytes;
    return result_splice_unsupported;
#endif
}

/* ------------------------------------------------------------------------- *
 * §3.1 P6 — slab buffer pool.
 *
 * One contiguous allocation holds `pool_count` * `buf_size` bytes; a
 * stack of free indices and a counting semaphore make `acquire` block
 * until a slot is free.  The pool itself is thread-safe across multiple
 * producers/consumers via an internal mutex.
 * ------------------------------------------------------------------------- */
struct socket_buf_pool
{
    uint8_t* base;
    size_t   buf_size;
    size_t   count;
    size_t*  free_idx;     /* stack */
    size_t   free_top;     /* index of next free slot in `free_idx` */
    pthread_mutex_t mu;
    sem_t    sem;
};

socket_buf_pool_t* socket_buf_pool_create(size_t buf_size, size_t pool_count)
{
    if (buf_size == 0 || pool_count == 0) return NULL;

    socket_buf_pool_t* p = (socket_buf_pool_t*)calloc(1, sizeof(*p));
    if (p == NULL) return NULL;
    p->base     = (uint8_t*)calloc(pool_count, buf_size);
    p->free_idx = (size_t*) calloc(pool_count, sizeof(size_t));
    if (p->base == NULL || p->free_idx == NULL)
    {
        free(p->base); free(p->free_idx); free(p);
        return NULL;
    }
    p->buf_size = buf_size;
    p->count    = pool_count;
    p->free_top = pool_count;
    for (size_t i = 0; i < pool_count; ++i)
    {
        p->free_idx[i] = i;
    }
    if (pthread_mutex_init(&p->mu, NULL) != 0 ||
        sem_init(&p->sem, 0, (unsigned)pool_count) != 0)
    {
        free(p->base); free(p->free_idx); free(p);
        return NULL;
    }
    return p;
}

void socket_buf_pool_destroy(socket_buf_pool_t* pool)
{
    if (pool == NULL) return;
    sem_destroy(&pool->sem);
    pthread_mutex_destroy(&pool->mu);
    free(pool->free_idx);
    free(pool->base);
    free(pool);
}

void* socket_buf_pool_acquire(socket_buf_pool_t* pool)
{
    if (pool == NULL) return NULL;
    if (sem_wait(&pool->sem) != 0) return NULL;
    pthread_mutex_lock(&pool->mu);
    size_t idx = pool->free_idx[--pool->free_top];
    pthread_mutex_unlock(&pool->mu);
    return pool->base + idx * pool->buf_size;
}

void socket_buf_pool_release(socket_buf_pool_t* pool, void* buf)
{
    if (pool == NULL || buf == NULL) return;
    size_t idx = ((uint8_t*)buf - pool->base) / pool->buf_size;
    if (idx >= pool->count) return; /* foreign pointer */
    pthread_mutex_lock(&pool->mu);
    pool->free_idx[pool->free_top++] = idx;
    pthread_mutex_unlock(&pool->mu);
    sem_post(&pool->sem);
}
