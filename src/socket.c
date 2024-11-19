
#include <socket.h>

#include <stdio.h>
#include <fcntl.h>
#include <netdb.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "niceties.h"

static bool is_socket_library_initialized = false;

static void socket_error(SSL *ssl, int err) {
  int socket_err = SSL_get_error(ssl, err);

  printf("Error: SSL_accept() error code %d\n", socket_err);
  if (socket_err == SSL_ERROR_SYSCALL) {
    if (err == -1) printf("  I/O error (%s)\n", strerror(errno));
    if (err == 0) printf("  SSL peer closed connection\n");
  }
}

result_t socket_init(socket_t* ssl1, socket_config_t* config)
{
  result_t result;

  if (!is_socket_library_initialized)
  {
    SSL_load_error_strings();
    SSL_library_init();
    is_socket_library_initialized = true;
  }

    int err;

    const SSL_METHOD *meth = SSLv23_server_method();
    ssl1->ctx = SSL_CTX_new(meth);
    if (ssl1->ctx == NULL) {
        result = error;
    }

  if (config->tls.certificate_file)
  {
    if (SSL_CTX_use_certificate_file(ssl1->ctx, config->tls.certificate_file, SSL_FILETYPE_PEM) <= 0) {
        printf("Error: Valid server certificate not found in %s\n", config->tls.certificate_file);
        result = error;
      }
  }

  if (config->tls.private_key_file)
  {
    if (SSL_CTX_use_PrivateKey_file(ssl1->ctx, config->tls.private_key_file, SSL_FILETYPE_PEM) <= 0) {
      printf("Error: Valid server private key not found in %s\n", config->tls.private_key_file);
      result = error;
    }
  }

  if (!SSL_CTX_check_private_key(ssl1->ctx)) {
    printf("Error: Private key does not match the certificate public key\n");
    result = error;
  }

  ssl1->listen_sd = socket(AF_INET, SOCK_STREAM, 0);
  if (ssl1->listen_sd == -1) result = error;
  
  memset(&ssl1->sa_serv, '\0', sizeof(ssl1->sa_serv));
  ssl1->sa_serv.sin_family      = AF_INET;
  ssl1->sa_serv.sin_addr.s_addr = INADDR_ANY;
  ssl1->sa_serv.sin_port        = htons(config->local.port);          /* Server Port number */
  
  err = bind(ssl1->listen_sd, (struct sockaddr*) &ssl1->sa_serv, sizeof (ssl1->sa_serv));
  if (err == -1) {
    printf("Error: Could not bind to port %d (%s)\n", config->local.port, strerror(errno));
    result = error;
  }
	     
  err = listen(ssl1->listen_sd, 5);
  if (err == -1) {
    printf("Error: listen() (%s)\n", strerror(errno));
    result = error;
  }
  else
  {
    result = ok;
  }

  return result;
}

result_t socket_deinit(socket_t* socket)
{
  result_t result;

  if (socket == NULL)
  {
    result = invalid_argument;
  }
  else
  {
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

    if (socket->ssl != NULL)
    {
      SSL_free(socket->ssl);
      socket->ssl = NULL;
    }

    if (socket->ctx != NULL)
    {
      SSL_CTX_free(socket->ctx);
      socket->ctx = NULL;
    }

    result = ok;
  }

  return result;
}


result_t socket_accept(socket_t* server, socket_t* client)
{
  result_t result = ok;
  int err;

  client->client_len = sizeof(client->sa_cli);
  client->sd = accept(server->listen_sd, (struct sockaddr*)&client->sa_cli, &client->client_len);
  if (client->sd == -1) {
    printf("Error: accept() (%s)\n", strerror(errno));
    result = error;
  }
  //close(ssl1->listen_sd);

  {
    char buffer[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client->sa_cli.sin_addr.s_addr, buffer, sizeof(buffer));
    printf("Connection from %s:%d\n", buffer, ntohs(client->sa_cli.sin_port));
  }
  
  /* ----------------------------------------------- */
  /* TCP connection is ready. Do server side SSL. */

  client->ssl = SSL_new(server->ctx);
  SSL_set_fd(client->ssl, client->sd);
  err = SSL_accept(client->ssl);
  if (err != 1) {
    socket_error(client->ssl, err);
    result = error;
  }
  else
  {
    printf ("SSL connection using %s\n", SSL_get_cipher (client->ssl));
    
    /* Get client's certificate */

    client->client_cert = SSL_get_peer_certificate (client->ssl);
    if (client->client_cert != NULL) {
      printf ("Client certificate:\n");
      
      client->str = X509_NAME_oneline (X509_get_subject_name (client->client_cert), 0, 0);
      if (!client->str) result = ERROR;
      printf ("\t subject: %s\n", client->str);
      OPENSSL_free (client->str);
      
      client->str = X509_NAME_oneline (X509_get_issuer_name  (client->client_cert), 0, 0);
      if (!client->str) result = ERROR;
      printf ("\t issuer: %s\n", client->str);
      OPENSSL_free (client->str);
      
      /* We could do all sorts of certificate verification stuff here before
        deallocating the certificate. */
      
      X509_free (client->client_cert);
    } else {
      printf ("Client does not have certificate.\n");
    }

    result = ok;
  }

  return result;
}

result_t socket_connect(socket_t* client)
{
  result_t result;

  if (client == NULL)
  {
    result = invalid_argument;
  }
  else
  {
    
    result = ok;
  }
}


result_t socket_read(socket_t* ssl1, span_t buffer, span_t* out_read)
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

            switch(reason)
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

result_t socket_write(socket_t* ssl1, span_t data)
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