
#include <socket.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/crypto.h>
#include <openssl/bio.h>

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "logging_simple.h"
#include "niceties.h"

#define SSL_DO_HANDSHAKE_SUCCESS 1

static bool is_socket_library_initialized = false;

static void socket_error(SSL *ssl, int err)
{
    int socket_err = SSL_get_error(ssl, err);

    printf("Error: SSL_accept() error code %d\n",
           socket_err); // TODO: change this to logs, remove printf.

    if (socket_err == SSL_ERROR_SYSCALL)
    {
        if (err == -1)
            printf("  I/O error (%s)\n",
                   strerror(errno)); // TODO: change this to logs, remove printf.
        if (err == 0)
            printf("  SSL peer closed connection\n"); // TODO: change this to logs,
                                                      // remove printf.
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

result_t socket_init(socket_t *ssl1, socket_config_t *config)
{
    if (!is_socket_library_initialized)
    {
        SSL_load_error_strings();
        SSL_library_init();
        is_socket_library_initialized = true;
    }

    int socket_result;
    const SSL_METHOD *ssl_method = (config->role == socket_role_server ? TLS_server_method() : TLS_client_method());

    (void)memset(ssl1, 0, sizeof(socket_t));
    
    ssl1->role = config->role;
    ssl1->local = config->local;
    ssl1->remote = config->remote;
    ssl1->ctx = SSL_CTX_new(ssl_method);

    if (ssl1->ctx == NULL)
    {
        return error;
    }
    
    if (config->tls.trusted_certificate_file != NULL &&
        // add_certificate_to_store(ssl1->ctx, config->tls.trusted_certificate_file) != 0)
        SSL_CTX_load_verify_locations(ssl1->ctx, config->tls.trusted_certificate_file, NULL) != 1)
    {
        // printf("Error: add_certificate_to_store failed\n");
        printf("Error: SSL_CTX_load_verify_locations failed\n");
        return error;            
    }

    if (config->tls.certificate_file != NULL &&
        SSL_CTX_use_certificate_file(ssl1->ctx, config->tls.certificate_file, SSL_FILETYPE_PEM) <= 0)
    {
        printf("Error: SSL_CTX_use_certificate_file failed\n");
        return error;
    }

    if (config->tls.private_key_file != NULL)
    {
        if (SSL_CTX_use_PrivateKey_file(ssl1->ctx, config->tls.private_key_file, SSL_FILETYPE_PEM) <= 0)
        {
            printf("Error: SSL_CTX_use_PrivateKey_file failed\n");
            return error;
        }

        if (!SSL_CTX_check_private_key(ssl1->ctx))
        {
            printf("Error: Private key does not match the certificate public key\n"); // TODO: change this to logs,
                                                                                    // remove printf.
            return error;
        }
    }

    if (config->role == socket_role_server)
    {
        ssl1->listen_sd = socket(AF_INET, SOCK_STREAM, 0);

        if (ssl1->listen_sd == -1)
        {
            return error;
        }

        memset(&ssl1->sa_serv, '\0', sizeof(ssl1->sa_serv));
        ssl1->sa_serv.sin_family = AF_INET;
        ssl1->sa_serv.sin_addr.s_addr = INADDR_ANY;
        ssl1->sa_serv.sin_port = htons(config->local.port); /* Server Port number */

        socket_result = bind(ssl1->listen_sd, (struct sockaddr *)&ssl1->sa_serv, sizeof(ssl1->sa_serv));

        if (socket_result == -1)
        {
            printf("Error: Could not bind to port %d (%s)\n", config->local.port,
                   strerror(errno)); // TODO: change this to logs, remove printf.
            return error;
        }

        socket_result = listen(ssl1->listen_sd, 5);

        if (socket_result == -1)
        {
            printf("Error: listen() (%s)\n",
                   strerror(errno)); // TODO: change this to logs, remove printf.
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
        if (socket->ssl != NULL)
        {
            (void)SSL_shutdown(socket->ssl);
            SSL_free(socket->ssl);
            socket->ssl = NULL;
        }

        if (socket->ctx != NULL)
        {
            SSL_CTX_free(socket->ctx);
            socket->ctx = NULL;
        }

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
    client->client_len = sizeof(client->sa_cli);
    client->sd = accept(server->listen_sd, (struct sockaddr *)&client->sa_cli, &client->client_len);

    if (client->sd == -1)
    {
        printf("Error: accept() (%s)\n", strerror(errno));
        result = error;
    }
    // close(ssl1->listen_sd);

    char buffer[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client->sa_cli.sin_addr.s_addr, buffer, sizeof(buffer));
    printf("Connection from %s:%d\n", buffer, ntohs(client->sa_cli.sin_port));

    /* ----------------------------------------------- */
    /* TCP connection is ready. Do server side SSL. */

    client->ssl = SSL_new(server->ctx);
    // SSL_set_msg_callback(client->ssl, SSL_debug);

    SSL_set_fd(client->ssl, client->sd);

    err = SSL_accept(client->ssl);

    if (err != 1)
    {
        socket_error(client->ssl, err);
        log_error("SSL_accept (%s)", strerror(errno));
        result = error;
    }
    else
    {
        printf("SSL connection using %s\n", SSL_get_cipher(client->ssl));

        /* Get client's certificate */

        check_peer_certificates(client->ssl, "client");

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
        hints.ai_socktype = SOCK_STREAM; // TODO: review this.

        uint8_t port_string[6] = {0};

        if (span_is_empty(span_copy_int32(span_from_memory(port_string), client->remote.port, NULL)))
        {
            fprintf(stderr, "span_copy_int32\n"); // TODO: review this. Log instead?
            return error;
        }

        if ((rv = getaddrinfo(span_get_ptr(client->remote.hostname), port_string, &hints, &servinfo)) != 0)
        {
            fprintf(stderr, "getaddrinfo: %s\n",
                    gai_strerror(rv)); // TODO: review this. Log instead?
            return error;
        }

        for (p = servinfo; p != NULL; p = p->ai_next)
        {
            if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
            {
                perror("client: socket");
                continue;
            }

            if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1)
            {
                close(sockfd);
                // perror("client: connect");
                continue;
            }

            break;
        }

        freeaddrinfo(servinfo);

        if (p == NULL)
        {
            fprintf(stderr, "client: failed to connect\n");
            result = error;
        }
        else
        {
            client->sd = sockfd;

            client->ssl = SSL_new(client->ctx);
            // SSL_set_msg_callback(client->ssl, SSL_debug);

            SSL_set_verify(client->ssl, SSL_VERIFY_PEER, NULL);

            err = SSL_set_fd(client->ssl, client->sd);

            if (err != 1)
            {
                // TODO: destroy SSL components.
                socket_error(client->ssl, err);
                result = error;
            }
            else
            {
                ERR_clear_error();
                err = SSL_connect(client->ssl);

                if (err != SSL_DO_HANDSHAKE_SUCCESS)
                {
                    int ssl_err = SSL_get_error(client->ssl, err);

                    if (ssl_err == SSL_ERROR_SSL)
                    {
                        printf("%s\n", ERR_error_string(ERR_get_error(), NULL));
                    }
                    else
                    {
                        printf("SSL handshake failed: %d\n", ssl_err);
                    }
                }

                printf("SSL_get_verify_result=%ld\n", SSL_get_verify_result(client->ssl)); // Results in: /usr/include/openssl/x509_vfy.h
                check_peer_certificates(client->ssl, "server");
                result = ok;
            }
        }
    }

    return result;
}

result_t socket_read(socket_t *ssl1, span_t buffer, span_t *out_read, span_t* remainder)
{
    result_t result;

    if (ssl1 == NULL || out_read == NULL)
    {
        result = invalid_argument;
    }
    else
    {
        int bytes_read;

        bytes_read = SSL_read(ssl1->ssl, span_get_ptr(buffer), span_get_size(buffer));

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
            int reason = SSL_get_error(ssl1->ssl, bytes_read);

            switch (reason)
            {
            case SSL_ERROR_ZERO_RETURN:
            case SSL_ERROR_SYSCALL:
            case SSL_ERROR_SSL:
                result = error;
                break;
            default:
                if (remainder != NULL)
                {
                    *remainder = buffer;
                }
                result = no_data;
                break;
            };
        }
    }

    return result;
}

result_t socket_write(socket_t *ssl1, span_t data)
{
    result_t result;

    if (ssl1 == NULL)
    {
        result = invalid_argument;
    }
    else
    {
        result = ok;

        while (span_get_size(data) > 0)
        {
            int n = SSL_write(ssl1->ssl, span_get_ptr(data), span_get_size(data));

            if (n <= 0)
            {
                result = error;
                log_error("SSL_write (%d)", SSL_get_error(ssl1->ssl, n));
                perror("SSL_write");
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
