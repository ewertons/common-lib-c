#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

#include "tests.h"
#include "socketx.h"
#include "logging.h"

#include <unistd.h> 
#include <string.h>
#include <time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

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
 * this covers the retry after a TCP failure, which predates this change. */
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

/* Binds a listener on exactly the address getaddrinfo handed back, so the
 * test controls which candidate is which rather than hoping for an order.
 * Accepts and never speaks, which is what stalls a TLS handshake. */
static int silent_listener_on(const struct sockaddr* address, socklen_t length,
                              int family, uint16_t port)
{
    int fd = socket(family, SOCK_STREAM, 0);
    if (fd < 0)
    {
        return -1;
    }

    int reuse = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    /* A copy, so the port can be set without disturbing the resolver's list. */
    struct sockaddr_storage bind_address;
    (void)memcpy(&bind_address, address, length);
    if (family == AF_INET)
    {
        ((struct sockaddr_in*)&bind_address)->sin_port = htons(port);
    }
    else
    {
        ((struct sockaddr_in6*)&bind_address)->sin6_port = htons(port);
    }

    if (bind(fd, (struct sockaddr*)&bind_address, length) != 0 || listen(fd, 4) != 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}

static bool has_pending_connection(int listener)
{
    struct pollfd pfd = { .fd = listener, .events = POLLIN, .revents = 0 };
    return poll(&pfd, 1, 0) > 0;
}

/* The retry this change actually adds: moving on after a candidate's TLS
 * handshake fails, rather than after its TCP connect fails.
 *
 * Both listeners accept and then say nothing, so every candidate stalls in
 * the handshake and the connect as a whole fails. That is the point -- what
 * is being asserted is that the second address was *tried*, which only
 * happens if a TLS failure no longer ends the search. Deterministic because
 * the listeners are bound onto the resolver's own list, whatever order it
 * returns, and fixture-free because no handshake ever completes.
 */
static void socket_connect_tries_the_next_address_after_a_tls_failure(void** state)
{
    (void)state;

    struct addrinfo hints;
    (void)memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* resolved = NULL;
    if (getaddrinfo("localhost", "0", &hints, &resolved) != 0 || resolved == NULL)
    {
        skip();  /* no resolver here; nothing to say about ordering */
    }

    struct addrinfo* first  = resolved;
    struct addrinfo* second = resolved->ai_next;
    if (second == NULL)
    {
        freeaddrinfo(resolved);
        skip();  /* localhost is single-homed on this machine */
    }

    /* One port, both addresses. Taken from the first bind so the test never
     * collides with whatever else is running. */
    int first_fd = silent_listener_on(first->ai_addr, first->ai_addrlen,
                                      first->ai_family, 0);
    assert_true(first_fd >= 0);

    struct sockaddr_storage bound;
    socklen_t bound_length = sizeof(bound);
    assert_int_equal(getsockname(first_fd, (struct sockaddr*)&bound, &bound_length), 0);
    uint16_t port = (first->ai_family == AF_INET)
                        ? ntohs(((struct sockaddr_in*)&bound)->sin_port)
                        : ntohs(((struct sockaddr_in6*)&bound)->sin6_port);

    int second_fd = silent_listener_on(second->ai_addr, second->ai_addrlen,
                                       second->ai_family, port);
    freeaddrinfo(resolved);
    if (second_fd < 0)
    {
        close(first_fd);
        skip();  /* the port was not free on the second address */
    }

    socket_t client;
    socket_config_t cfg = socket_get_default_secure_client_config();
    cfg.tls.enable      = true;
    cfg.remote.hostname = span_from_str_literal("localhost");
    cfg.remote.port     = port;
    cfg.io_timeout_ms   = 300;

    assert_int_equal(socket_init(&client, &cfg), ok);

    /* Every candidate stalls, so the whole connect fails. */
    assert_int_not_equal(socket_connect(&client), ok);

    /* The assertion that matters: both were attempted. Before the handshake
     * moved inside the loop, the first failure ended it and the second
     * listener never saw a connection. */
    assert_true(has_pending_connection(first_fd));
    assert_true(has_pending_connection(second_fd));

    (void)socket_deinit(&client);
    close(first_fd);
    close(second_fd);
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

/* --- proxy (HTTP CONNECT) ----------------------------------------------- *
 *
 * A minimal CONNECT proxy on loopback, one connection per test. Whatever
 * host the client asks for, it dials 127.0.0.1:target_port -- which is also
 * what lets a test ask for a name that does not resolve anywhere and prove
 * the client never tried to resolve it itself.
 */

typedef enum test_proxy_behavior
{
    test_proxy_relay,
    test_proxy_answer_with_tunnel_bytes,
    test_proxy_refuse,
    test_proxy_close_early,
    test_proxy_silent,
    test_proxy_malformed_status_line,
} test_proxy_behavior_t;

typedef struct test_proxy
{
    test_proxy_behavior_t behavior;
    int listener;
    uint16_t port;
    uint16_t target_port;
    char request[2048];
    size_t request_length;
    pthread_t thread;
} test_proxy_t;

static int loopback_listener(uint16_t* out_port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert_true(fd >= 0);

    struct sockaddr_in address;
    (void)memset(&address, 0, sizeof(address));
    address.sin_family      = AF_INET;
    address.sin_port        = 0;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    assert_int_equal(bind(fd, (struct sockaddr*)&address, sizeof(address)), 0);
    assert_int_equal(listen(fd, 4), 0);

    socklen_t length = sizeof(address);
    assert_int_equal(getsockname(fd, (struct sockaddr*)&address, &length), 0);
    *out_port = ntohs(address.sin_port);
    return fd;
}

static void send_text(int fd, const char* text)
{
    (void)send(fd, text, strlen(text), MSG_NOSIGNAL);
}

static void relay(int first, int second)
{
    uint8_t buffer[4096];
    for (;;)
    {
        struct pollfd descriptors[2] = {
            { .fd = first,  .events = POLLIN, .revents = 0 },
            { .fd = second, .events = POLLIN, .revents = 0 },
        };
        if (poll(descriptors, 2, 10000) <= 0)
        {
            return;
        }
        for (int index = 0; index < 2; index++)
        {
            if (descriptors[index].revents == 0)
            {
                continue;
            }
            int from = index == 0 ? first : second;
            int to   = index == 0 ? second : first;
            ssize_t received = recv(from, buffer, sizeof(buffer), 0);
            if (received <= 0 || send(to, buffer, (size_t)received, MSG_NOSIGNAL) != received)
            {
                return;
            }
        }
    }
}

static void* test_proxy_run(void* argument)
{
    test_proxy_t* proxy = (test_proxy_t*)argument;

    int client = accept(proxy->listener, NULL, NULL);
    if (client < 0)
    {
        return NULL;
    }

    while (proxy->request_length < sizeof(proxy->request) - 1 &&
           strstr(proxy->request, "\r\n\r\n") == NULL)
    {
        ssize_t received = recv(client, proxy->request + proxy->request_length,
                                sizeof(proxy->request) - 1 - proxy->request_length, 0);
        if (received <= 0)
        {
            break;
        }
        proxy->request_length += (size_t)received;
    }

    switch (proxy->behavior)
    {
        case test_proxy_relay:
        {
            int target = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in address;
            (void)memset(&address, 0, sizeof(address));
            address.sin_family      = AF_INET;
            address.sin_port        = htons(proxy->target_port);
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (target >= 0 && connect(target, (struct sockaddr*)&address, sizeof(address)) == 0)
            {
                send_text(client, "HTTP/1.1 200 Connection established\r\nProxy-Agent: test\r\n\r\n");
                relay(client, target);
            }
            else
            {
                send_text(client, "HTTP/1.1 502 Bad Gateway\r\n\r\n");
            }
            if (target >= 0)
            {
                close(target);
            }
            break;
        }
        case test_proxy_answer_with_tunnel_bytes:
            /* One write, so the client sees the answer and the remote's first
             * bytes together and must not swallow the latter. */
            send_text(client, "HTTP/1.0 200 OK\r\n\r\nHELLO");
            {
                char drain[64];
                while (recv(client, drain, sizeof(drain), 0) > 0) { }
            }
            break;
        case test_proxy_refuse:
            send_text(client, "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n");
            break;
        case test_proxy_close_early:
            send_text(client, "HTTP/1.1 20");
            break;
        case test_proxy_silent:
        {
            char drain[64];
            while (recv(client, drain, sizeof(drain), 0) > 0) { }
            break;
        }
        case test_proxy_malformed_status_line:
            send_text(client, "SSH-2.0-OpenSSH_9.6\r\n\r\n");
            break;
    }

    close(client);
    return NULL;
}

static void test_proxy_start(test_proxy_t* proxy, test_proxy_behavior_t behavior, uint16_t target_port)
{
    (void)memset(proxy, 0, sizeof(*proxy));
    proxy->behavior    = behavior;
    proxy->target_port = target_port;
    proxy->listener    = loopback_listener(&proxy->port);
    assert_int_equal(pthread_create(&proxy->thread, NULL, test_proxy_run, proxy), 0);
}

static void test_proxy_stop(test_proxy_t* proxy)
{
    /* Wakes an accept() that never got a connection. */
    (void)shutdown(proxy->listener, SHUT_RDWR);
    assert_int_equal(pthread_join(proxy->thread, NULL), 0);
    close(proxy->listener);
}

static socket_config_t proxied_plain_client(const char* remote_hostname, uint16_t remote_port,
                                            uint16_t proxy_port, uint32_t io_timeout_ms)
{
    socket_config_t cfg = socket_get_default_plain_client_config();
    cfg.remote.hostname = span_init((uint8_t*)(uintptr_t)remote_hostname, (uint32_t)strlen(remote_hostname));
    cfg.remote.port     = remote_port;
    cfg.proxy.hostname  = span_from_str_literal("127.0.0.1");
    cfg.proxy.port      = proxy_port;
    cfg.io_timeout_ms   = io_timeout_ms;
    return cfg;
}

static void socket_default_client_configs_have_no_proxy(void** state)
{
    (void)state;
    socket_config_t secure = socket_get_default_secure_client_config();
    socket_config_t plain  = socket_get_default_plain_client_config();

    assert_true(span_is_empty(secure.proxy.hostname));
    assert_true(span_is_empty(secure.proxy.authorization));
    assert_true(span_is_empty(plain.proxy.hostname));
}

static void socket_proxy_tunnels_a_plain_connection(void** state)
{
    (void)state;
    uint16_t target_port;
    int target_listener = loopback_listener(&target_port);

    test_proxy_t proxy;
    test_proxy_start(&proxy, test_proxy_relay, target_port);

    /* Not resolvable here: only the proxy may resolve the remote name. */
    socket_config_t cfg = proxied_plain_client("remote.invalid", target_port, proxy.port, 5000);
    cfg.proxy.authorization = span_from_str_literal("Basic dGVzdDpzZWNyZXQ=");

    socket_t client;
    assert_int_equal(socket_init(&client, &cfg), ok);
    assert_int_equal(socket_connect(&client), ok);

    int target = accept(target_listener, NULL, NULL);
    assert_true(target >= 0);

    assert_int_equal(socket_write(&client, span_from_str_literal("ping")), ok);
    char received[8] = { 0 };
    assert_int_equal(recv(target, received, 4, MSG_WAITALL), 4);
    assert_memory_equal(received, "ping", 4);

    send_text(target, "pong");
    uint8_t raw[8];
    span_t got;
    assert_int_equal(socket_read(&client, span_from_memory(raw), &got, NULL), ok);
    assert_int_equal(span_get_size(got), 4);
    assert_memory_equal(span_get_ptr(got), "pong", 4);

    assert_int_equal(socket_deinit(&client), ok);
    close(target);
    test_proxy_stop(&proxy);
    close(target_listener);

    char expected[128];
    (void)snprintf(expected, sizeof(expected),
                   "CONNECT remote.invalid:%u HTTP/1.1\r\nHost: remote.invalid:%u\r\n",
                   target_port, target_port);
    assert_true(strncmp(proxy.request, expected, strlen(expected)) == 0);
    assert_non_null(strstr(proxy.request, "\r\nProxy-Authorization: Basic dGVzdDpzZWNyZXQ=\r\n"));
    assert_int_equal(strcmp(proxy.request + proxy.request_length - 4, "\r\n\r\n"), 0);
}

static void socket_proxy_without_authorization_sends_no_header(void** state)
{
    (void)state;
    uint16_t target_port;
    int target_listener = loopback_listener(&target_port);

    test_proxy_t proxy;
    test_proxy_start(&proxy, test_proxy_relay, target_port);

    socket_config_t cfg = proxied_plain_client("localhost", target_port, proxy.port, 5000);
    socket_t client;
    assert_int_equal(socket_init(&client, &cfg), ok);
    assert_int_equal(socket_connect(&client), ok);

    assert_int_equal(socket_deinit(&client), ok);
    test_proxy_stop(&proxy);
    close(target_listener);

    assert_null(strstr(proxy.request, "Proxy-Authorization"));
}

/* Bytes that arrive in the same segment as the proxy's answer belong to the
 * tunnel and must reach the caller. */
static void socket_proxy_keeps_bytes_that_follow_its_answer(void** state)
{
    (void)state;
    test_proxy_t proxy;
    test_proxy_start(&proxy, test_proxy_answer_with_tunnel_bytes, 0);

    socket_config_t cfg = proxied_plain_client("localhost", 80, proxy.port, 5000);
    socket_t client;
    assert_int_equal(socket_init(&client, &cfg), ok);
    assert_int_equal(socket_connect(&client), ok);

    uint8_t raw[16];
    span_t got;
    assert_int_equal(socket_read(&client, span_from_memory(raw), &got, NULL), ok);
    assert_int_equal(span_get_size(got), 5);
    assert_memory_equal(span_get_ptr(got), "HELLO", 5);

    assert_int_equal(socket_deinit(&client), ok);
    test_proxy_stop(&proxy);
}

static bool tls_fixtures_present(void)
{
    return access(CA_CHAIN_PATH, R_OK) == 0 && access(SERVER_CERT_PATH, R_OK) == 0;
}

static result_t proxied_tls_connect(const char* remote_hostname, int port, bool exchange_data)
{
    socket_config_t server_cfg = socket_get_default_secure_server_config();
    server_cfg.local.port             = port;
    server_cfg.local.address          = "127.0.0.1";
    server_cfg.tls.certificate_file   = SERVER_CERT_PATH;
    server_cfg.tls.private_key_file   = SERVER_PK_PATH;

    socket_t listener;
    socket_t server;
    assert_int_equal(socket_init(&listener, &server_cfg), ok);

    test_proxy_t proxy;
    test_proxy_start(&proxy, test_proxy_relay, (uint16_t)port);

    socket_config_t cfg = socket_get_default_secure_client_config();
    cfg.remote.hostname = span_init((uint8_t*)(uintptr_t)remote_hostname, (uint32_t)strlen(remote_hostname));
    cfg.remote.port     = port;
    cfg.proxy.hostname  = span_from_str_literal("127.0.0.1");
    cfg.proxy.port      = proxy.port;
    cfg.io_timeout_ms   = 5000;
    cfg.tls.trusted_certificate_file = CA_CHAIN_PATH;

    task_t* accept_task = socket_accept_async(&listener, &server);
    assert_non_null(accept_task);

    socket_t client;
    assert_int_equal(socket_init(&client, &cfg), ok);
    result_t result = socket_connect(&client);

    if (result == ok && exchange_data)
    {
        assert_true(task_wait(accept_task));
        assert_int_equal(socket_write(&client, span_from_str_literal("secret")), ok);
        uint8_t raw[16];
        span_t got;
        assert_int_equal(socket_read(&server, span_from_memory(raw), &got, NULL), ok);
        assert_int_equal(span_get_size(got), 6);
        assert_memory_equal(span_get_ptr(got), "secret", 6);
    }

    (void)socket_deinit(&client);
    (void)task_wait(accept_task);
    task_release(accept_task);
    (void)socket_deinit(&server);
    test_proxy_stop(&proxy);
    (void)socket_deinit(&listener);

    return result;
}

/* TLS runs end-to-end through the tunnel, verified against the remote name. */
static void socket_proxy_tunnels_tls_to_the_remote(void** state)
{
    (void)state;
    if (!tls_fixtures_present())
    {
        skip();
    }
    assert_int_equal(proxied_tls_connect("localhost", 5601, true), ok);
}

/* Same tunnel, same server, but the remote name is not the certificate's:
 * verification must be against the name asked for, not the proxy or the
 * address that ended up answering. */
static void socket_proxy_tls_rejects_a_certificate_for_another_name(void** state)
{
    (void)state;
    if (!tls_fixtures_present())
    {
        skip();
    }
    assert_int_not_equal(proxied_tls_connect("not-localhost.invalid", 5602, false), ok);
}

static char captured_log[4096];

static void capture_log(log_level_t level, const char* file, int line,
                        const char* message, va_list arguments, void* user_context)
{
    (void)level; (void)file; (void)line; (void)user_context;
    size_t used = strlen(captured_log);
    if (used < sizeof(captured_log) - 1)
    {
        (void)vsnprintf(captured_log + used, sizeof(captured_log) - used, message, arguments);
        (void)strncat(captured_log, "\n", sizeof(captured_log) - strlen(captured_log) - 1);
    }
}

/* The status is what tells an operator why; the credentials must never show. */
static void socket_proxy_refusal_fails_the_connect(void** state)
{
    (void)state;
    test_proxy_t proxy;
    test_proxy_start(&proxy, test_proxy_refuse, 0);

    socket_config_t cfg = proxied_plain_client("localhost", 443, proxy.port, 5000);
    cfg.proxy.authorization = span_from_str_literal("Basic c2VjcmV0LXZhbHVl");
    socket_t client;
    assert_int_equal(socket_init(&client, &cfg), ok);

    captured_log[0] = '\0';
    log_set_global_sink(capture_log, LOG_TRACE, NULL);
    result_t result = socket_connect(&client);
    log_set_global_sink(NULL, LOG_OFF, NULL);

    assert_int_equal(result, error);
    assert_int_equal(client.sd, -1);
    assert_non_null(strstr(captured_log, "status 403"));
    assert_null(strstr(captured_log, "c2VjcmV0LXZhbHVl"));

    (void)socket_deinit(&client);
    test_proxy_stop(&proxy);
}

static void socket_proxy_closing_early_fails_the_connect(void** state)
{
    (void)state;
    test_proxy_t proxy;
    test_proxy_start(&proxy, test_proxy_close_early, 0);

    socket_config_t cfg = proxied_plain_client("localhost", 443, proxy.port, 5000);
    socket_t client;
    assert_int_equal(socket_init(&client, &cfg), ok);
    assert_int_equal(socket_connect(&client), error);

    (void)socket_deinit(&client);
    test_proxy_stop(&proxy);
}

static void socket_proxy_malformed_status_line_fails_the_connect(void** state)
{
    (void)state;
    test_proxy_t proxy;
    test_proxy_start(&proxy, test_proxy_malformed_status_line, 0);

    socket_config_t cfg = proxied_plain_client("localhost", 443, proxy.port, 5000);
    socket_t client;
    assert_int_equal(socket_init(&client, &cfg), ok);
    assert_int_equal(socket_connect(&client), error);

    (void)socket_deinit(&client);
    test_proxy_stop(&proxy);
}

static void socket_proxy_that_never_answers_times_out(void** state)
{
    (void)state;
    test_proxy_t proxy;
    test_proxy_start(&proxy, test_proxy_silent, 0);

    socket_config_t cfg = proxied_plain_client("localhost", 443, proxy.port, 500);
    socket_t client;
    assert_int_equal(socket_init(&client, &cfg), ok);

    uint64_t started = monotonic_ms();
    assert_int_equal(socket_connect(&client), error);
    uint64_t elapsed = monotonic_ms() - started;

    assert_true(elapsed >= 400);
    assert_true(elapsed < 3000);

    (void)socket_deinit(&client);
    test_proxy_stop(&proxy);
}

static void socket_proxy_rejects_header_injection(void** state)
{
    (void)state;
    uint16_t proxy_port;
    int listener = loopback_listener(&proxy_port);

    socket_config_t cfg = proxied_plain_client("localhost", 443, proxy_port, 1000);
    cfg.proxy.authorization = span_from_str_literal("Basic x\r\nX-Injected: 1");

    socket_t client;
    assert_int_equal(socket_init(&client, &cfg), ok);
    assert_int_equal(socket_connect(&client), invalid_argument);

    (void)socket_deinit(&client);
    close(listener);
}

static void socket_proxy_port_out_of_range_is_rejected(void** state)
{
    (void)state;
    socket_t client;

    socket_config_t zero = proxied_plain_client("localhost", 443, 0, 0);
    assert_int_equal(socket_init(&client, &zero), invalid_argument);

    socket_config_t too_large = proxied_plain_client("localhost", 443, 0, 0);
    too_large.proxy.port = 70000;
    assert_int_equal(socket_init(&client, &too_large), invalid_argument);
}

static void socket_proxy_is_not_supported_by_the_non_blocking_connect(void** state)
{
    (void)state;
    socket_config_t cfg = proxied_plain_client("localhost", 443, 3128, 0);
    socket_t client;
    assert_int_equal(socket_init(&client, &cfg), ok);
    assert_int_equal(socket_connect_nb_begin(&client), invalid_argument);
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
      cmocka_unit_test(socket_io_timeout_reaches_the_descriptor),
      cmocka_unit_test(socket_without_an_io_timeout_stays_blocking),
      cmocka_unit_test(socket_read_gives_up_on_a_silent_peer),
      cmocka_unit_test(socket_connect_tries_every_resolved_address),
      cmocka_unit_test(socket_connect_tries_the_next_address_after_a_tls_failure),
      cmocka_unit_test(socket_connect_leaves_the_descriptor_blocking),
      cmocka_unit_test(socket_connect_reports_a_refused_port_promptly),
      cmocka_unit_test(socket_connect_gives_up_on_an_address_that_swallows_syns),
      cmocka_unit_test(socket_default_client_configs_have_no_proxy),
      cmocka_unit_test(socket_proxy_tunnels_a_plain_connection),
      cmocka_unit_test(socket_proxy_without_authorization_sends_no_header),
      cmocka_unit_test(socket_proxy_keeps_bytes_that_follow_its_answer),
      cmocka_unit_test(socket_proxy_tunnels_tls_to_the_remote),
      cmocka_unit_test(socket_proxy_tls_rejects_a_certificate_for_another_name),
      cmocka_unit_test(socket_proxy_refusal_fails_the_connect),
      cmocka_unit_test(socket_proxy_closing_early_fails_the_connect),
      cmocka_unit_test(socket_proxy_malformed_status_line_fails_the_connect),
      cmocka_unit_test(socket_proxy_that_never_answers_times_out),
      cmocka_unit_test(socket_proxy_rejects_header_injection),
      cmocka_unit_test(socket_proxy_port_out_of_range_is_rejected),
      cmocka_unit_test(socket_proxy_is_not_supported_by_the_non_blocking_connect),
  };

  return cmocka_run_group_tests_name("socket_client_and_server_success", tests, NULL, NULL);
}
