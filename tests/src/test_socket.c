#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

#include "tests.h"
#include "socketx.h"

#include <unistd.h> 

#define CLIENT_CERT_PATH "/tmp/http-c-certs/client/client.cert.pem"
#define CLIENT_PK_PATH "/tmp/http-c-certs/client/client.key.pem"
#define SERVER_CERT_PATH "/tmp/http-c-certs/server/server.cert.pem"
#define SERVER_PK_PATH "/tmp/http-c-certs/server/server.key.pem"
#define CA_CHAIN_PATH "/tmp/http-c-certs/ca/chain.ca.cert.pem"

static void socket_client_and_server_success(void** state)
{
    (void)state;
    int port = 5578;
    uint8_t raw_write_buffer[10];
    uint8_t raw_read_buffer[10];
    span_t read_buffer = span_from_memory(raw_read_buffer);

    socket_config_t client_socket_config = socket_get_default_secure_client_config();
    client_socket_config.remote.hostname = span_from_str_literal("localhost");
    client_socket_config.remote.port = port;
    client_socket_config.tls.certificate_file = CLIENT_CERT_PATH;
    client_socket_config.tls.private_key_file = CLIENT_PK_PATH;
    client_socket_config.tls.trusted_certificate_file = CA_CHAIN_PATH;

    socket_config_t server_socket_config = socket_get_default_secure_server_config();
    server_socket_config.local.port = port;
    server_socket_config.tls.certificate_file = SERVER_CERT_PATH;
    server_socket_config.tls.private_key_file = SERVER_PK_PATH;

    socket_t client_socket;
    socket_t server_listen_socket;
    socket_t server_socket;

    assert_int_equal(socket_init(&client_socket, &client_socket_config), ok);
    assert_int_equal(socket_init(&server_listen_socket, &server_socket_config), ok);

    task_t* accept_task = socket_accept_async(&server_listen_socket, &server_socket);
    assert_non_null(accept_task);

    assert_int_equal(socket_connect(&client_socket), ok);

    assert_true(task_wait(accept_task));
    task_release(accept_task);

    for (int i = 0; i < 10; i++)
    {
      span_t bytes_read;
      span_t write_buffer = span_from_memory(raw_write_buffer);
      write_buffer = span_copy_int32(write_buffer, i, NULL);

      assert_int_not_equal(0, span_get_size(write_buffer));
      assert_int_equal(socket_write(&client_socket, write_buffer), ok);
      assert_int_equal(socket_read(&server_socket, read_buffer, &bytes_read, NULL), ok);
      assert_int_equal(span_get_size(bytes_read), span_get_size(write_buffer));
      assert_memory_equal(span_get_ptr(bytes_read), span_get_ptr(write_buffer), span_get_size(bytes_read));
    }

    assert_int_equal(socket_deinit(&client_socket), ok);
    assert_int_equal(socket_deinit(&server_socket), ok);
}

static void socket_set_nonblocking_invalid_fd_fails(void** state)
{
    (void)state;
    assert_int_not_equal(socket_set_nonblocking(-1), ok);
}

static void socket_set_nonblocking_valid_fd_succeeds(void** state)
{
    (void)state;
    int fds[2];
    assert_int_equal(pipe(fds), 0);
    assert_int_equal(socket_set_nonblocking(fds[0]), ok);
    assert_int_equal(socket_set_nonblocking(fds[1]), ok);
    close(fds[0]);
    close(fds[1]);
}

static void socket_get_io_want_on_null_returns_zero(void** state)
{
    (void)state;
    assert_int_equal(socket_get_io_want(NULL), 0);
}

static void socket_get_default_secure_server_config_has_expected_values(void** state)
{
    (void)state;

    socket_config_t cfg = socket_get_default_secure_server_config();

    assert_int_equal(socket_role_server, cfg.role);
    assert_true(cfg.tls.enable);
    assert_int_equal(DEFAULT_LISTENING_PORT, cfg.local.port);
}

static void socket_get_default_secure_client_config_has_expected_values(void** state)
{
    (void)state;

    socket_config_t cfg = socket_get_default_secure_client_config();

    assert_int_equal(socket_role_client, cfg.role);
    assert_true(cfg.tls.enable);
    assert_int_equal(0, cfg.local.port);
}

/* --- local.address ------------------------------------------------------ */

/* TLS is off in these: they exercise bind(), not the handshake, and the TLS
 * fixtures under /tmp are not present on every machine that runs the suite. */
static socket_config_t plain_listener(int port, const char* address)
{
    socket_config_t cfg = socket_get_default_secure_server_config();
    cfg.tls.enable    = false;
    cfg.local.port    = port;
    cfg.local.address = address;
    return cfg;
}

static void socket_bind_address_unset_binds_every_interface(void** state)
{
    (void)state;
    socket_t listener;
    socket_config_t cfg = plain_listener(5581, NULL);

    assert_int_equal(socket_init(&listener, &cfg), ok);
    assert_int_equal(socket_deinit(&listener), ok);
}

static void socket_bind_address_empty_binds_every_interface(void** state)
{
    (void)state;
    socket_t listener;
    socket_config_t cfg = plain_listener(5582, "");

    assert_int_equal(socket_init(&listener, &cfg), ok);
    assert_int_equal(socket_deinit(&listener), ok);
}

static void socket_bind_address_loopback_succeeds(void** state)
{
    (void)state;
    socket_t listener;
    socket_config_t cfg = plain_listener(5583, "127.0.0.1");

    assert_int_equal(socket_init(&listener, &cfg), ok);
    assert_int_equal(socket_deinit(&listener), ok);
}

/* The security-relevant one. A rejected address must not fall back to binding
 * every interface, and must not leave anything bound behind: the whole point
 * of the field is to NOT be reachable, so a silent 0.0.0.0 would invert it. */
static void socket_bind_address_malformed_is_rejected_without_binding(void** state)
{
    (void)state;
    socket_t rejected;
    socket_config_t bad = plain_listener(5584, "not-an-address");

    assert_int_equal(socket_init(&rejected, &bad), invalid_argument);

    /* If the failed attempt had bound (or leaked a bound descriptor), this
     * second listener on the same port could not come up. */
    socket_t listener;
    socket_config_t good = plain_listener(5584, "127.0.0.1");

    assert_int_equal(socket_init(&listener, &good), ok);
    assert_int_equal(socket_deinit(&listener), ok);
}

/* Documented contract: inet_pton only. A name could resolve to an address on
 * an interface the caller never meant to expose, so it is refused rather than
 * looked up. */
static void socket_bind_address_rejects_names_and_ipv6(void** state)
{
    (void)state;
    socket_t listener;

    socket_config_t by_name = plain_listener(5585, "localhost");
    assert_int_equal(socket_init(&listener, &by_name), invalid_argument);

    socket_config_t ipv6 = plain_listener(5585, "::1");
    assert_int_equal(socket_init(&listener, &ipv6), invalid_argument);

    socket_config_t truncated = plain_listener(5585, "127.0.0");
    assert_int_equal(socket_init(&listener, &truncated), invalid_argument);
}

static void socket_bind_address_is_ignored_for_a_client(void** state)
{
    (void)state;
    socket_t client;

    socket_config_t cfg = socket_get_default_secure_client_config();
    cfg.tls.enable    = false;
    cfg.local.address = "not-an-address"; /* meaningless for a client */

    assert_int_equal(socket_init(&client, &cfg), ok);
    assert_int_equal(socket_deinit(&client), ok);
}

int test_socket()
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(socket_client_and_server_success),
      cmocka_unit_test(socket_set_nonblocking_invalid_fd_fails),
      cmocka_unit_test(socket_set_nonblocking_valid_fd_succeeds),
      cmocka_unit_test(socket_get_io_want_on_null_returns_zero),
      cmocka_unit_test(socket_get_default_secure_server_config_has_expected_values),
      cmocka_unit_test(socket_get_default_secure_client_config_has_expected_values),
      cmocka_unit_test(socket_bind_address_unset_binds_every_interface),
      cmocka_unit_test(socket_bind_address_empty_binds_every_interface),
      cmocka_unit_test(socket_bind_address_loopback_succeeds),
      cmocka_unit_test(socket_bind_address_malformed_is_rejected_without_binding),
      cmocka_unit_test(socket_bind_address_rejects_names_and_ipv6),
      cmocka_unit_test(socket_bind_address_is_ignored_for_a_client),
  };

  return cmocka_run_group_tests_name("socket_client_and_server_success", tests, NULL, NULL);
}
