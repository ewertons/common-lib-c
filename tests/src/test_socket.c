#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

#include "tests.h"
#include "socketx.h"

#include <unistd.h> 
#include <string.h>
#include <time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

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

/* --- io_timeout_ms ------------------------------------------------------ *
 *
 * The case these exist for: an address that completes the TCP handshake and
 * then never speaks. A black-holed route does exactly that, and without a
 * timeout the TLS handshake keeps the calling thread for the life of the
 * process -- which is a dashboard whose sign-in never returns, not a slow
 * request. Plain TCP throughout, so no TLS fixtures are needed.
 */

static uint64_t monotonic_ms(void)
{
    struct timespec ts;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

/* A listener that accepts and then says nothing, which is what makes the
 * client's handshake wait. Nothing is ever read from it. */
static int silent_listener(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert_true(fd >= 0);

    int reuse = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    (void)memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    assert_int_equal(bind(fd, (struct sockaddr*)&addr, sizeof(addr)), 0);
    assert_int_equal(listen(fd, 4), 0);
    return fd;
}

static socket_config_t plain_client(int port, uint32_t io_timeout_ms)
{
    socket_config_t cfg = socket_get_default_secure_client_config();
    cfg.tls.enable      = false;
    cfg.remote.hostname = span_from_str_literal("127.0.0.1");
    cfg.remote.port     = port;
    cfg.io_timeout_ms   = io_timeout_ms;
    return cfg;
}

/* The timeout has to reach the descriptor, because that is the only thing
 * that bounds a handshake. Read it back rather than trusting the call. */
static void socket_io_timeout_reaches_the_descriptor(void** state)
{
    (void)state;
    int listener = silent_listener(5591);

    socket_t client;
    socket_config_t cfg = plain_client(5591, 1500);

    assert_int_equal(socket_init(&client, &cfg), ok);
    assert_int_equal(socket_connect(&client), ok);

    struct timeval tv;
    socklen_t len = sizeof(tv);
    assert_int_equal(getsockopt(client.sd, SOL_SOCKET, SO_RCVTIMEO, &tv, &len), 0);
    assert_int_equal((int)tv.tv_sec, 1);
    assert_int_equal((int)tv.tv_usec, 500000);

    assert_int_equal(socket_deinit(&client), ok);
    close(listener);
}

/* Zero has to keep waiting forever, or setting nothing would silently
 * change how every existing caller behaves. */
static void socket_without_an_io_timeout_stays_blocking(void** state)
{
    (void)state;
    int listener = silent_listener(5592);

    socket_t client;
    socket_config_t cfg = plain_client(5592, 0);

    assert_int_equal(socket_init(&client, &cfg), ok);
    assert_int_equal(socket_connect(&client), ok);

    struct timeval tv;
    socklen_t len = sizeof(tv);
    assert_int_equal(getsockopt(client.sd, SOL_SOCKET, SO_RCVTIMEO, &tv, &len), 0);
    assert_int_equal((int)tv.tv_sec, 0);
    assert_int_equal((int)tv.tv_usec, 0);

    assert_int_equal(socket_deinit(&client), ok);
    close(listener);
}

/* The one that matters. Against a peer that accepts and then says nothing,
 * a read must come back rather than park the thread. Before the timeout
 * existed this call never returned. */
static void socket_read_gives_up_on_a_silent_peer(void** state)
{
    (void)state;
    int listener = silent_listener(5593);

    socket_t client;
    socket_config_t cfg = plain_client(5593, 400);

    assert_int_equal(socket_init(&client, &cfg), ok);
    assert_int_equal(socket_connect(&client), ok);

    uint8_t  raw[64];
    span_t   got;
    uint64_t started = monotonic_ms();
    result_t result  = socket_read(&client, span_from_memory(raw), &got, NULL);
    uint64_t elapsed = monotonic_ms() - started;

    /* try_again is the honest answer for a receive timeout: nothing arrived
     * this time round. What matters is that it answered at all. */
    assert_int_equal(result, try_again);
    assert_true(elapsed >= 300);
    assert_true(elapsed < 5000);

    assert_int_equal(socket_deinit(&client), ok);
    close(listener);
}

/* A name that resolves to several addresses must not be sunk by the first
 * one being unusable. 127.0.0.1 has nothing listening on this port, so the
 * connect fails and the loop has to carry on to the address that works --
 * the same path a black-holed address takes after its handshake fails. */
static void socket_connect_tries_every_resolved_address(void** state)
{
    (void)state;
    /* localhost resolves to ::1 and 127.0.0.1 on most systems; binding only
     * IPv4 means whichever is offered first may be the one that fails. */
    int listener = silent_listener(5594);

    socket_t client;
    socket_config_t cfg = socket_get_default_secure_client_config();
    cfg.tls.enable      = false;
    cfg.remote.hostname = span_from_str_literal("localhost");
    cfg.remote.port     = 5594;
    cfg.io_timeout_ms   = 400;

    assert_int_equal(socket_init(&client, &cfg), ok);
    assert_int_equal(socket_connect(&client), ok);

    assert_int_equal(socket_deinit(&client), ok);
    close(listener);
}

/* The connect is attempted non-blocking so it can be given a deadline, but
 * everything downstream is written against a blocking descriptor. Leaving
 * O_NONBLOCK set would turn every later read into a spurious try_again, so
 * this is the assertion that keeps the deadline from breaking the rest. */
static void socket_connect_leaves_the_descriptor_blocking(void** state)
{
    (void)state;
    int listener = silent_listener(5595);

    socket_t client;
    socket_config_t cfg = plain_client(5595, 2000);

    assert_int_equal(socket_init(&client, &cfg), ok);
    assert_int_equal(socket_connect(&client), ok);

    int flags = fcntl(client.sd, F_GETFL, 0);
    assert_true(flags != -1);
    assert_int_equal(flags & O_NONBLOCK, 0);

    assert_int_equal(socket_deinit(&client), ok);
    close(listener);
}

/* A refused connection reports ready on POLLOUT just as a successful one
 * does, so the deadline path has to check SO_ERROR rather than treat
 * "finished" as "connected". Nothing listens on this port. */
static void socket_connect_reports_a_refused_port_promptly(void** state)
{
    (void)state;
    socket_t client;
    socket_config_t cfg = plain_client(5596, 5000);

    assert_int_equal(socket_init(&client, &cfg), ok);

    uint64_t started = monotonic_ms();
    assert_int_not_equal(socket_connect(&client), ok);
    uint64_t elapsed = monotonic_ms() - started;

    /* Refused is immediate; the point is that it is not reported as a
     * success and does not sit out the budget. */
    assert_true(elapsed < 2000);

    (void)socket_deinit(&client);
}

/* The case the deadline exists for: an address that swallows SYNs. Without
 * it the kernel alone decides, which is tcp_syn_retries -- measured at
 * 135 s on the machine this was written for, and per address.
 *
 * 192.0.2.1 is TEST-NET-1: routable, assigned to nobody, and normally
 * dropped rather than refused. Some networks answer with an unreachable
 * instead, which is a legitimate fast failure, so the timing is only
 * asserted when the address actually behaved like a hole. */
static void socket_connect_gives_up_on_an_address_that_swallows_syns(void** state)
{
    (void)state;
    socket_t client;
    socket_config_t cfg = socket_get_default_secure_client_config();
    cfg.tls.enable      = false;
    cfg.remote.hostname = span_from_str_literal("192.0.2.1");
    cfg.remote.port     = 443;
    cfg.io_timeout_ms   = 700;

    assert_int_equal(socket_init(&client, &cfg), ok);

    uint64_t started = monotonic_ms();
    assert_int_not_equal(socket_connect(&client), ok);
    uint64_t elapsed = monotonic_ms() - started;

    if (elapsed > 200)
    {
        /* It was dropped, so the deadline is what ended it. Well under the
         * kernel's two minutes is the whole point. */
        assert_true(elapsed < 10000);
    }

    (void)socket_deinit(&client);
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
      cmocka_unit_test(socket_io_timeout_reaches_the_descriptor),
      cmocka_unit_test(socket_without_an_io_timeout_stays_blocking),
      cmocka_unit_test(socket_read_gives_up_on_a_silent_peer),
      cmocka_unit_test(socket_connect_tries_every_resolved_address),
      cmocka_unit_test(socket_connect_leaves_the_descriptor_blocking),
      cmocka_unit_test(socket_connect_reports_a_refused_port_promptly),
      cmocka_unit_test(socket_connect_gives_up_on_an_address_that_swallows_syns),
  };

  return cmocka_run_group_tests_name("socket_client_and_server_success", tests, NULL, NULL);
}
