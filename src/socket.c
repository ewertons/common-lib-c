
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
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "logging_simple.h"
#include "niceties.h"

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

static void SSL_debug(int write_p, int version,
                                         int content_type, const void *buf,
                                         size_t len, SSL *ssl, void *arg)
{
  (void)ssl;
  (void)arg;
  printf("%s [%d][%d] %.*s\n", write_p == 0 ? "<-" : "->", version, content_type, (int)len, (char*)buf);
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
        add_certificate_to_store(ssl1->ctx, config->tls.trusted_certificate_file) != 0)
    {
        printf("Error: add_certificate_to_store failed\n");
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
    SSL_set_msg_callback(client->ssl, SSL_debug);

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

        client->client_cert = SSL_get_peer_certificate(client->ssl);
        if (client->client_cert != NULL)
        {
            printf("Client certificate:\n");

            client->str = X509_NAME_oneline(X509_get_subject_name(client->client_cert), 0, 0);
            if (!client->str)
                result = ERROR;
            printf("\t subject: %s\n", client->str);
            OPENSSL_free(client->str);

            client->str = X509_NAME_oneline(X509_get_issuer_name(client->client_cert), 0, 0);
            if (!client->str)
                result = ERROR;
            printf("\t issuer: %s\n", client->str);
            OPENSSL_free(client->str);

            /* We could do all sorts of certificate verification stuff here before
              deallocating the certificate. */

            X509_free(client->client_cert);
        }
        else
        {
            printf("Client does not have certificate.\n");
        }

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
            SSL_set_msg_callback(client->ssl, SSL_debug);

            err = SSL_set_fd(client->ssl, client->sd);

            if (err != 1)
            {
                // TODO: destroy SSL components.
                socket_error(client->ssl, err);
                result = error;
            }
            else
            {
                SSL_set_connect_state(client->ssl);
                result = ok;
            }
        }
    }

    return result;
}

result_t socket_read(socket_t *ssl1, span_t buffer, span_t *out_read)
{
    result_t result;

    if (ssl1 == NULL)
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
                result = ok;
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