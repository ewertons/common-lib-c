/* ------------------------------------------------------------------------- *
 * Tests for the socket.h §3 improvement-plan additions.
 *
 * Covered features:
 *   - Default-config helpers now set tcp.nodelay=true and
 *     io_model=socket_io_model_epoll (4 helpers).
 *   - New result_t values: result_connection_refused,
 *     result_splice_unsupported.
 *   - socket_buf_pool_*: create / acquire / release / destroy.
 *   - TCP tuning knobs (TCP_NODELAY, SO_SNDBUF, SO_RCVBUF, SO_KEEPALIVE).
 *   - SO_REUSEPORT (multi-listener on the same port).
 *   - accept4() — the accepted fd is non-blocking.
 *   - socket_writev_nb plain-TCP fast path + argument guards.
 *   - socket_splice plain-TCP fast path + TLS rejection.
 *   - TLS 1.3 only / cipher_suites / curves.
 *   - require_peer_cert (mTLS).
 *   - SPKI pinning match / mismatch (non-blocking handshake path).
 * ------------------------------------------------------------------------- */

#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <setjmp.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/evp.h>

#include <cmocka.h>

#include "tests.h"
#include "socket.h"
#include "niceties.h"

#define CLIENT_CERT_PATH        "/tmp/http-c-certs/client/client.cert.pem"
#define CLIENT_PK_PATH          "/tmp/http-c-certs/client/client.key.pem"
#define SERVER_CERT_PATH        "/tmp/http-c-certs/server/server.cert.pem"
#define SERVER_PK_PATH          "/tmp/http-c-certs/server/server.key.pem"
#define CA_CHAIN_PATH           "/tmp/http-c-certs/ca/chain.ca.cert.pem"

/* Distinct ports per test so re-runs after a crash don't fight TIME_WAIT,
 * and so a test that fails before deinit cannot block the next one with
 * a still-LISTENing leaked fd. */
#define PORT_REUSEPORT          5810
#define PORT_ACCEPT4            5811
#define PORT_WRITEV             5812
#define PORT_SPLICE_A           5813
#define PORT_SPLICE_B           5814
#define PORT_TLS13              5815
#define PORT_CIPHER             5816
#define PORT_MTLS_REJECT        5817
#define PORT_MTLS_ACCEPT        5818
#define PORT_PIN_OK             5819
#define PORT_PIN_BAD            5820

/* ─────────────────────────────────────────────────────────────────────── *
 * Small helpers
 * ─────────────────────────────────────────────────────────────────────── */

static void drive_handshake_pair(socket_t* a, socket_t* b,
                                 result_t* a_out, result_t* b_out)
{
    /* Drive a NB TLS handshake between two non-blocking peers until both
     * sides reach a terminal state.  Bounded by an iteration cap so a
     * stuck handshake fails the test instead of hanging. */
    *a_out = try_again;
    *b_out = try_again;
    for (int i = 0; i < 200; ++i)
    {
        if (*a_out == try_again) *a_out = socket_handshake_nb(a);
        if (*b_out == try_again) *b_out = socket_handshake_nb(b);
        if (*a_out != try_again && *b_out != try_again) return;
        usleep(2000);
    }
}

/* Compute the SHA-256 hash of the SubjectPublicKeyInfo of the cert at
 * @p path and write it into the 32-byte buffer @p out. */
static bool sha256_spki_of_cert_file(const char* path, uint8_t out[32])
{
    bool ok_ret = false;
    FILE* fp = fopen(path, "r");
    if (fp == NULL) return false;
    X509* cert = PEM_read_X509(fp, NULL, NULL, NULL);
    fclose(fp);
    if (cert == NULL) return false;

    uint8_t* der = NULL;
    int der_len = i2d_X509_PUBKEY(X509_get_X509_PUBKEY(cert), &der);
    if (der_len > 0 && der != NULL)
    {
        unsigned int hlen = 0;
        if (EVP_Digest(der, (size_t)der_len, out, &hlen, EVP_sha256(), NULL) == 1
            && hlen == 32)
        {
            ok_ret = true;
        }
        OPENSSL_free(der);
    }
    X509_free(cert);
    return ok_ret;
}

/* Drive the client connect→handshake loop and the server accept→handshake
 * loop in lockstep until both reach a terminal state. */
static void drive_tls_session(socket_t* client_listen_or_null,
                              socket_t* server_listen,
                              socket_config_t* client_cfg,
                              socket_t* client_out,
                              socket_t* accepted_out,
                              result_t* client_terminal,
                              result_t* server_terminal)
{
    (void)client_listen_or_null;

    /* Server listener must be non-blocking for accept_nb to return
     * try_again instead of stalling. */
    assert_int_equal(socket_set_nonblocking(server_listen->listen_sd), ok);

    /* Init client and kick off the TCP SYN. */
    assert_int_equal(socket_init(client_out, client_cfg), ok);
    assert_int_equal(socket_connect_nb_begin(client_out), ok);

    /* Loop accept_nb until we have an accepted client. */
    result_t ar = try_again;
    for (int i = 0; i < 500 && ar == try_again; ++i)
    {
        ar = socket_accept_nb(server_listen, accepted_out);
        if (ar == try_again) usleep(2000);
    }
    assert_int_equal(ar, ok);

    /* Drive both sides until each reaches a terminal state. The client
     * side first finishes its TCP connect via socket_connect_nb_continue
     * (which also lazily creates the client SSL object), then drives
     * SSL_do_handshake via socket_handshake_nb. */
    bool client_setup_done = false;
    *client_terminal = try_again;
    *server_terminal = try_again;
    for (int i = 0; i < 1000; ++i)
    {
        if (*client_terminal == try_again)
        {
            if (!client_setup_done)
            {
                result_t cr = socket_connect_nb_continue(client_out);
                if (cr == ok)
                {
                    client_setup_done = true;
                    /* For plain-TCP clients connect_nb_continue marks the
                     * handshake done already; reflect that. */
                    if (!client_out->tls.enabled) *client_terminal = ok;
                }
                else if (cr != try_again)
                {
                    *client_terminal = cr;
                }
            }
            else
            {
                *client_terminal = socket_handshake_nb(client_out);
            }
        }
        if (*server_terminal == try_again)
        {
            *server_terminal = socket_handshake_nb(accepted_out);
        }
        if (*client_terminal != try_again && *server_terminal != try_again) return;
        usleep(2000);
    }
}

/* ─────────────────────────────────────────────────────────────────────── *
 * 1. Default-config helpers
 * ─────────────────────────────────────────────────────────────────────── */

static void default_helpers_set_nodelay_and_epoll(void** state)
{
    (void)state;
    socket_config_t a = socket_get_default_secure_server_config();
    socket_config_t b = socket_get_default_secure_client_config();
    socket_config_t c = socket_get_default_plain_server_config();
    socket_config_t d = socket_get_default_plain_client_config();

    assert_true(a.tcp.nodelay);
    assert_true(b.tcp.nodelay);
    assert_true(c.tcp.nodelay);
    assert_true(d.tcp.nodelay);

    assert_int_equal(a.io_model, socket_io_model_epoll);
    assert_int_equal(b.io_model, socket_io_model_epoll);
    assert_int_equal(c.io_model, socket_io_model_epoll);
    assert_int_equal(d.io_model, socket_io_model_epoll);
}

/* ─────────────────────────────────────────────────────────────────────── *
 * 2. New result codes are distinct
 * ─────────────────────────────────────────────────────────────────────── */

static void new_result_codes_are_distinct(void** state)
{
    (void)state;
    assert_int_not_equal(result_connection_refused, error);
    assert_int_not_equal(result_splice_unsupported, error);
    assert_int_not_equal(result_connection_refused, result_splice_unsupported);
    assert_int_not_equal(result_connection_refused, ok);
    assert_int_not_equal(result_splice_unsupported, ok);
}

/* ─────────────────────────────────────────────────────────────────────── *
 * 3. socket_buf_pool
 * ─────────────────────────────────────────────────────────────────────── */

static void buf_pool_zero_args_returns_null(void** state)
{
    (void)state;
    assert_null(socket_buf_pool_create(0, 4));
    assert_null(socket_buf_pool_create(64, 0));
}

static void buf_pool_acquire_release_round_trip(void** state)
{
    (void)state;
    const size_t BUF = 64;
    const size_t N   = 4;
    socket_buf_pool_t* pool = socket_buf_pool_create(BUF, N);
    assert_non_null(pool);

    void* held[N];
    for (size_t i = 0; i < N; ++i)
    {
        held[i] = socket_buf_pool_acquire(pool);
        assert_non_null(held[i]);
        /* Distinct addresses, suitable for writing BUF bytes. */
        for (size_t j = 0; j < i; ++j) assert_ptr_not_equal(held[i], held[j]);
        memset(held[i], (int)('a' + i), BUF);
    }

    /* Release them and re-acquire — each address must come back exactly once. */
    for (size_t i = 0; i < N; ++i) socket_buf_pool_release(pool, held[i]);
    bool seen[N];
    memset(seen, 0, sizeof(seen));
    for (size_t i = 0; i < N; ++i)
    {
        void* p = socket_buf_pool_acquire(pool);
        assert_non_null(p);
        bool found = false;
        for (size_t j = 0; j < N; ++j)
        {
            if (held[j] == p && !seen[j]) { seen[j] = true; found = true; break; }
        }
        assert_true(found);
    }

    /* Foreign pointer release is a no-op (does not crash, does not free). */
    int dummy;
    socket_buf_pool_release(pool, &dummy);
    socket_buf_pool_release(NULL, NULL);

    socket_buf_pool_destroy(pool);
    socket_buf_pool_destroy(NULL); /* must not crash. */
}

/* ─────────────────────────────────────────────────────────────────────── *
 * 4. TCP tuning knobs end up on the listening socket
 * ─────────────────────────────────────────────────────────────────────── */

static void tcp_knobs_applied_to_listen_socket(void** state)
{
    (void)state;
    socket_config_t cfg  = socket_get_default_plain_server_config();
    cfg.local.port       = 0; /* OS-assigned ephemeral */
    cfg.tcp.nodelay      = true;
    cfg.tcp.send_buf_size = 131072;
    cfg.tcp.recv_buf_size = 131072;
    cfg.tcp.keepalive_idle_sec = 42;

    socket_t s;
    assert_int_equal(socket_init(&s, &cfg), ok);

    int v = 0; socklen_t vlen = sizeof(v);
    assert_int_equal(getsockopt(s.listen_sd, IPPROTO_TCP, TCP_NODELAY, &v, &vlen), 0);
    assert_int_not_equal(v, 0);

    v = 0; vlen = sizeof(v);
    assert_int_equal(getsockopt(s.listen_sd, SOL_SOCKET, SO_KEEPALIVE, &v, &vlen), 0);
    assert_int_not_equal(v, 0);

    int snd = 0; vlen = sizeof(snd);
    assert_int_equal(getsockopt(s.listen_sd, SOL_SOCKET, SO_SNDBUF, &snd, &vlen), 0);
    /* Linux doubles the value internally; just require >= what we asked. */
    assert_true(snd >= 131072);

    int rcv = 0; vlen = sizeof(rcv);
    assert_int_equal(getsockopt(s.listen_sd, SOL_SOCKET, SO_RCVBUF, &rcv, &vlen), 0);
    assert_true(rcv >= 131072);

    assert_int_equal(socket_deinit(&s), ok);
}

/* ─────────────────────────────────────────────────────────────────────── *
 * 5. SO_REUSEPORT lets two listeners co-exist on the same port
 * ─────────────────────────────────────────────────────────────────────── */

static void reuse_port_allows_dual_bind(void** state)
{
    (void)state;
    socket_config_t cfg = socket_get_default_plain_server_config();
    cfg.local.port       = PORT_REUSEPORT;
    cfg.local.reuse_port = true;

    socket_t a, b;
    assert_int_equal(socket_init(&a, &cfg), ok);
    assert_int_equal(socket_init(&b, &cfg), ok);

    assert_int_equal(socket_deinit(&a), ok);
    assert_int_equal(socket_deinit(&b), ok);
}

/* ─────────────────────────────────────────────────────────────────────── *
 * 6. accept4() yields an O_NONBLOCK fd
 * ─────────────────────────────────────────────────────────────────────── */

static void accept_nb_returns_nonblocking_fd(void** state)
{
    (void)state;
    socket_config_t scfg = socket_get_default_plain_server_config();
    scfg.local.port = PORT_ACCEPT4;
    socket_t srv;
    assert_int_equal(socket_init(&srv, &scfg), ok);
    assert_int_equal(socket_set_nonblocking(srv.listen_sd), ok);

    /* Plain TCP client created via raw socket() so we don't pull in the
     * full client init/connect flow. */
    int cfd = socket(AF_INET, SOCK_STREAM, 0);
    assert_int_not_equal(cfd, -1);
    struct sockaddr_in sa = { 0 };
    sa.sin_family = AF_INET;
    sa.sin_port   = htons(PORT_ACCEPT4);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert_int_equal(connect(cfd, (struct sockaddr*)&sa, sizeof(sa)), 0);

    socket_t accepted;
    result_t ar = try_again;
    for (int i = 0; i < 200 && ar == try_again; ++i)
    {
        ar = socket_accept_nb(&srv, &accepted);
        if (ar == try_again) usleep(2000);
    }
    assert_int_equal(ar, ok);

    int flags = fcntl(accepted.sd, F_GETFL, 0);
    assert_int_not_equal(flags, -1);
    assert_true((flags & O_NONBLOCK) != 0);

    close(cfd);
    assert_int_equal(socket_deinit(&accepted), ok);
    assert_int_equal(socket_deinit(&srv), ok);
}

/* ─────────────────────────────────────────────────────────────────────── *
 * 7. socket_writev_nb plain-TCP fast path
 * ─────────────────────────────────────────────────────────────────────── */

static void writev_nb_plain_two_segments(void** state)
{
    (void)state;
    socket_config_t scfg = socket_get_default_plain_server_config();
    scfg.local.port = PORT_WRITEV;
    socket_t srv, accepted;
    assert_int_equal(socket_init(&srv, &scfg), ok);
    assert_int_equal(socket_set_nonblocking(srv.listen_sd), ok);

    int cfd = socket(AF_INET, SOCK_STREAM, 0);
    assert_int_not_equal(cfd, -1);
    struct sockaddr_in sa = { 0 };
    sa.sin_family = AF_INET;
    sa.sin_port = htons(PORT_WRITEV);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert_int_equal(connect(cfd, (struct sockaddr*)&sa, sizeof(sa)), 0);

    result_t ar = try_again;
    for (int i = 0; i < 200 && ar == try_again; ++i)
    {
        ar = socket_accept_nb(&srv, &accepted);
        if (ar == try_again) usleep(2000);
    }
    assert_int_equal(ar, ok);

    /* Build a temporary "client" socket_t backed by the raw cfd so we
     * can call socket_writev_nb on it. socket_writev_nb only consults
     * `tls.enabled` and `sd`. */
    socket_t cli = { 0 };
    cli.sd = cfd;
    cli.tls.enabled = false;
    cli.tcp_connected = true;

    uint8_t hdr[]  = { 'H', 'D', 'R' };
    uint8_t body[] = "PAYLOAD";
    span_t iov[2];
    iov[0] = span_init(hdr,  (uint32_t)sizeof(hdr));
    iov[1] = span_init(body, (uint32_t)sizeof(body) - 1u);

    uint32_t written = 0;
    result_t wr = socket_writev_nb(&cli, iov, 2, &written);
    assert_int_equal(wr, ok);
    assert_int_equal(written, sizeof(hdr) + sizeof(body) - 1u);

    /* Read on the accepted side until we have all bytes. */
    uint8_t buf[64] = { 0 };
    size_t got = 0;
    for (int i = 0; i < 200 && got < written; ++i)
    {
        uint32_t n = 0;
        result_t rr = socket_read_nb(&accepted,
            span_init(buf + got, (uint32_t)(sizeof(buf) - got)), &n);
        if (rr == ok) got += n;
        else if (rr == try_again) usleep(2000);
        else fail_msg("socket_read_nb returned 0x%x", rr);
    }
    assert_int_equal(got, written);
    assert_memory_equal(buf,                 hdr,  sizeof(hdr));
    assert_memory_equal(buf + sizeof(hdr),   body, sizeof(body) - 1u);

    close(cfd);
    assert_int_equal(socket_deinit(&accepted), ok);
    assert_int_equal(socket_deinit(&srv), ok);
}

/* ─────────────────────────────────────────────────────────────────────── *
 * 8. socket_writev_nb argument guards
 * ─────────────────────────────────────────────────────────────────────── */

static void writev_nb_argument_guards(void** state)
{
    (void)state;
    socket_t s = { 0 };
    s.tls.enabled = false;
    span_t iov;

    uint32_t w = 99;
    assert_int_equal(socket_writev_nb(NULL, &iov, 1, &w), invalid_argument);
    assert_int_equal(socket_writev_nb(&s,   &iov, 1, NULL), invalid_argument);
    assert_int_equal(socket_writev_nb(&s,   NULL, 1, &w), invalid_argument);

    /* iov_cnt == 0 → success, no bytes written. */
    w = 99;
    assert_int_equal(socket_writev_nb(&s, NULL, 0, &w), ok);
    assert_int_equal(w, 0);
}

/* ─────────────────────────────────────────────────────────────────────── *
 * 9. socket_splice rejects TLS sockets
 * ─────────────────────────────────────────────────────────────────────── */

static void splice_rejects_tls_sockets(void** state)
{
    (void)state;
    socket_t plain = { 0 };
    socket_t tls   = { 0 };
    tls.tls.enabled = true;

    size_t spliced = 0;
    assert_int_equal(socket_splice(&tls,   &plain, 4096, &spliced), result_splice_unsupported);
    assert_int_equal(socket_splice(&plain, &tls,   4096, &spliced), result_splice_unsupported);
}

/* ─────────────────────────────────────────────────────────────────────── *
 * 10. socket_splice plain-TCP success
 *
 *   client_a  ──►  srv_a ──(accept)──►  acc_a
 *                                       │
 *                                       │ socket_splice(acc_a → cli_b)
 *                                       ▼
 *   client_b  ──►  srv_b ──(accept)──►  acc_b   (← payload arrives here)
 *
 * cli_b is a raw-fd socket_t that we have connected ourselves so that
 * splice has a valid plain-TCP destination.
 * ─────────────────────────────────────────────────────────────────────── */

static int connect_plain_loopback(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) return -1;
    struct sockaddr_in sa = { 0 };
    sa.sin_family = AF_INET;
    sa.sin_port   = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr*)&sa, sizeof(sa)) != 0) { close(fd); return -1; }
    return fd;
}

static void splice_plain_tcp_proxy_payload(void** state)
{
    (void)state;
    socket_config_t cfg_a = socket_get_default_plain_server_config();
    cfg_a.local.port = PORT_SPLICE_A;
    socket_config_t cfg_b = socket_get_default_plain_server_config();
    cfg_b.local.port = PORT_SPLICE_B;

    socket_t srv_a, srv_b, acc_a, acc_b;
    assert_int_equal(socket_init(&srv_a, &cfg_a), ok);
    assert_int_equal(socket_init(&srv_b, &cfg_b), ok);
    assert_int_equal(socket_set_nonblocking(srv_a.listen_sd), ok);
    assert_int_equal(socket_set_nonblocking(srv_b.listen_sd), ok);

    int fd_client_a = connect_plain_loopback(PORT_SPLICE_A);
    int fd_client_b = connect_plain_loopback(PORT_SPLICE_B);
    assert_int_not_equal(fd_client_a, -1);
    assert_int_not_equal(fd_client_b, -1);

    result_t r = try_again;
    for (int i = 0; i < 200 && r == try_again; ++i)
    {
        r = socket_accept_nb(&srv_a, &acc_a);
        if (r == try_again) usleep(2000);
    }
    assert_int_equal(r, ok);
    r = try_again;
    for (int i = 0; i < 200 && r == try_again; ++i)
    {
        r = socket_accept_nb(&srv_b, &acc_b);
        if (r == try_again) usleep(2000);
    }
    assert_int_equal(r, ok);

    /* Wrap fd_client_b as a socket_t so socket_splice can use it as dst. */
    socket_t cli_b = { 0 };
    cli_b.sd = fd_client_b;
    cli_b.tls.enabled = false;
    cli_b.tcp_connected = true;

    /* Produce data on acc_a from fd_client_a. */
    static const char payload[] = "PROXY-ROUTE-DATA";
    ssize_t sent = send(fd_client_a, payload, sizeof(payload) - 1u, 0);
    assert_int_equal(sent, sizeof(payload) - 1u);

    /* Splice acc_a → cli_b. */
    size_t spliced = 0;
    result_t sr = try_again;
    for (int i = 0; i < 200 && spliced < sizeof(payload) - 1u; ++i)
    {
        size_t this_call = 0;
        sr = socket_splice(&acc_a, &cli_b, sizeof(payload) - 1u - spliced, &this_call);
        spliced += this_call;
        if (sr == try_again) usleep(2000);
        else if (sr != ok && sr != try_again) fail_msg("splice rc=0x%x", sr);
    }
    assert_int_equal(spliced, sizeof(payload) - 1u);

    /* Read on acc_b. */
    uint8_t out[64] = { 0 };
    size_t got = 0;
    for (int i = 0; i < 200 && got < sizeof(payload) - 1u; ++i)
    {
        uint32_t n = 0;
        result_t rr = socket_read_nb(&acc_b,
            span_init(out + got, (uint32_t)(sizeof(out) - got)), &n);
        if (rr == ok) got += n;
        else if (rr == try_again) usleep(2000);
        else fail_msg("read rc=0x%x", rr);
    }
    assert_int_equal(got, sizeof(payload) - 1u);
    assert_memory_equal(out, payload, sizeof(payload) - 1u);

    close(fd_client_a);
    close(fd_client_b);
    assert_int_equal(socket_deinit(&acc_a), ok);
    assert_int_equal(socket_deinit(&acc_b), ok);
    assert_int_equal(socket_deinit(&srv_a), ok);
    assert_int_equal(socket_deinit(&srv_b), ok);
}

/* ─────────────────────────────────────────────────────────────────────── *
 * 11. TLS 1.3 only succeeds end-to-end
 * ─────────────────────────────────────────────────────────────────────── */

static void tls13_only_handshake_succeeds(void** state)
{
    (void)state;
    socket_config_t scfg = socket_get_default_secure_server_config();
    scfg.local.port = PORT_TLS13;
    scfg.tls.certificate_file        = SERVER_CERT_PATH;
    scfg.tls.private_key_file        = SERVER_PK_PATH;
    scfg.tls.tls13_only              = true;

    socket_config_t ccfg = socket_get_default_secure_client_config();
    ccfg.remote.hostname             = span_from_str_literal("localhost");
    ccfg.remote.port                 = PORT_TLS13;
    ccfg.tls.trusted_certificate_file = CA_CHAIN_PATH;
    ccfg.tls.tls13_only              = true;

    socket_t srv, cli, acc;
    assert_int_equal(socket_init(&srv, &scfg), ok);

    socket_t cli_listen_unused;
    (void)cli_listen_unused;
    result_t ct, st;
    drive_tls_session(NULL, &srv, &ccfg, &cli, &acc, &ct, &st);
    assert_int_equal(ct, ok);
    assert_int_equal(st, ok);

    assert_int_equal(socket_deinit(&cli), ok);
    assert_int_equal(socket_deinit(&acc), ok);
    assert_int_equal(socket_deinit(&srv), ok);
}

/* ─────────────────────────────────────────────────────────────────────── *
 * 12. Explicit cipher_suites + curves succeed
 * ─────────────────────────────────────────────────────────────────────── */

static void cipher_suites_and_curves_handshake_succeeds(void** state)
{
    (void)state;
    socket_config_t scfg = socket_get_default_secure_server_config();
    scfg.local.port = PORT_CIPHER;
    scfg.tls.certificate_file        = SERVER_CERT_PATH;
    scfg.tls.private_key_file        = SERVER_PK_PATH;
    scfg.tls.tls13_only              = true;
    scfg.tls.cipher_suites           = "TLS_AES_256_GCM_SHA384";
    scfg.tls.curves                  = "X25519:P-256";

    socket_config_t ccfg = socket_get_default_secure_client_config();
    ccfg.remote.hostname             = span_from_str_literal("localhost");
    ccfg.remote.port                 = PORT_CIPHER;
    ccfg.tls.trusted_certificate_file = CA_CHAIN_PATH;
    ccfg.tls.tls13_only              = true;
    ccfg.tls.cipher_suites           = "TLS_AES_256_GCM_SHA384";
    ccfg.tls.curves                  = "X25519:P-256";

    socket_t srv, cli, acc;
    assert_int_equal(socket_init(&srv, &scfg), ok);

    result_t ct, st;
    drive_tls_session(NULL, &srv, &ccfg, &cli, &acc, &ct, &st);
    assert_int_equal(ct, ok);
    assert_int_equal(st, ok);

    assert_int_equal(socket_deinit(&cli), ok);
    assert_int_equal(socket_deinit(&acc), ok);
    assert_int_equal(socket_deinit(&srv), ok);
}

/* ─────────────────────────────────────────────────────────────────────── *
 * 13. require_peer_cert: missing client cert is rejected;
 *                       supplied client cert is accepted.
 * ─────────────────────────────────────────────────────────────────────── */

static void require_peer_cert_rejects_unauthenticated_client(void** state)
{
    (void)state;
    socket_config_t scfg = socket_get_default_secure_server_config();
    scfg.local.port = PORT_MTLS_REJECT;
    scfg.tls.certificate_file        = SERVER_CERT_PATH;
    scfg.tls.private_key_file        = SERVER_PK_PATH;
    scfg.tls.trusted_certificate_file = CA_CHAIN_PATH;
    scfg.tls.require_peer_cert       = true;

    socket_config_t ccfg = socket_get_default_secure_client_config();
    ccfg.remote.hostname             = span_from_str_literal("localhost");
    ccfg.remote.port                 = PORT_MTLS_REJECT;
    ccfg.tls.trusted_certificate_file = CA_CHAIN_PATH;
    /* No client cert — server must reject. */

    socket_t srv, cli, acc;
    assert_int_equal(socket_init(&srv, &scfg), ok);

    result_t ct, st;
    drive_tls_session(NULL, &srv, &ccfg, &cli, &acc, &ct, &st);
    /* At least one of the peers must report an error. */
    assert_true(ct != ok || st != ok);

    assert_int_equal(socket_deinit(&cli), ok);
    assert_int_equal(socket_deinit(&acc), ok);
    assert_int_equal(socket_deinit(&srv), ok);
}

static void require_peer_cert_accepts_authenticated_client(void** state)
{
    (void)state;
    socket_config_t scfg = socket_get_default_secure_server_config();
    scfg.local.port = PORT_MTLS_ACCEPT;
    scfg.tls.certificate_file        = SERVER_CERT_PATH;
    scfg.tls.private_key_file        = SERVER_PK_PATH;
    scfg.tls.trusted_certificate_file = CA_CHAIN_PATH;
    scfg.tls.require_peer_cert       = true;

    socket_config_t ccfg = socket_get_default_secure_client_config();
    ccfg.remote.hostname             = span_from_str_literal("localhost");
    ccfg.remote.port                 = PORT_MTLS_ACCEPT;
    ccfg.tls.certificate_file        = CLIENT_CERT_PATH;
    ccfg.tls.private_key_file        = CLIENT_PK_PATH;
    ccfg.tls.trusted_certificate_file = CA_CHAIN_PATH;

    socket_t srv, cli, acc;
    assert_int_equal(socket_init(&srv, &scfg), ok);

    result_t ct, st;
    drive_tls_session(NULL, &srv, &ccfg, &cli, &acc, &ct, &st);
    assert_int_equal(ct, ok);
    assert_int_equal(st, ok);

    assert_int_equal(socket_deinit(&cli), ok);
    assert_int_equal(socket_deinit(&acc), ok);
    assert_int_equal(socket_deinit(&srv), ok);
}

/* ─────────────────────────────────────────────────────────────────────── *
 * 14./15. SPKI pinning (server-side pin of the *client* SPKI).
 *
 * The pin is configured on the server side; it inspects the client's
 * presented certificate after the NB handshake.  This exercises the
 * SPKI-extraction + SHA-256 + memcmp path inside socket_handshake_nb.
 * ─────────────────────────────────────────────────────────────────────── */

static void spki_pin_match_succeeds(void** state)
{
    (void)state;
    uint8_t pin[32];
    if (!sha256_spki_of_cert_file(CLIENT_CERT_PATH, pin))
    {
        skip(); return;
    }

    socket_config_t scfg = socket_get_default_secure_server_config();
    scfg.local.port = PORT_PIN_OK;
    scfg.tls.certificate_file        = SERVER_CERT_PATH;
    scfg.tls.private_key_file        = SERVER_PK_PATH;
    scfg.tls.trusted_certificate_file = CA_CHAIN_PATH;
    scfg.tls.require_peer_cert       = true;
    scfg.tls.pinned_spki_sha256      = pin;

    socket_config_t ccfg = socket_get_default_secure_client_config();
    ccfg.remote.hostname             = span_from_str_literal("localhost");
    ccfg.remote.port                 = PORT_PIN_OK;
    ccfg.tls.certificate_file        = CLIENT_CERT_PATH;
    ccfg.tls.private_key_file        = CLIENT_PK_PATH;
    ccfg.tls.trusted_certificate_file = CA_CHAIN_PATH;

    socket_t srv, cli, acc;
    assert_int_equal(socket_init(&srv, &scfg), ok);

    result_t ct, st;
    drive_tls_session(NULL, &srv, &ccfg, &cli, &acc, &ct, &st);
    assert_int_equal(ct, ok);
    assert_int_equal(st, ok);

    assert_int_equal(socket_deinit(&cli), ok);
    assert_int_equal(socket_deinit(&acc), ok);
    assert_int_equal(socket_deinit(&srv), ok);
}

static void spki_pin_mismatch_rejects(void** state)
{
    (void)state;
    static const uint8_t bogus_pin[32] = {
        0xde,0xad,0xbe,0xef,0xde,0xad,0xbe,0xef,
        0xde,0xad,0xbe,0xef,0xde,0xad,0xbe,0xef,
        0xde,0xad,0xbe,0xef,0xde,0xad,0xbe,0xef,
        0xde,0xad,0xbe,0xef,0xde,0xad,0xbe,0xef,
    };

    socket_config_t scfg = socket_get_default_secure_server_config();
    scfg.local.port = PORT_PIN_BAD;
    scfg.tls.certificate_file        = SERVER_CERT_PATH;
    scfg.tls.private_key_file        = SERVER_PK_PATH;
    scfg.tls.trusted_certificate_file = CA_CHAIN_PATH;
    scfg.tls.require_peer_cert       = true;
    scfg.tls.pinned_spki_sha256      = bogus_pin;

    socket_config_t ccfg = socket_get_default_secure_client_config();
    ccfg.remote.hostname             = span_from_str_literal("localhost");
    ccfg.remote.port                 = PORT_PIN_BAD;
    ccfg.tls.certificate_file        = CLIENT_CERT_PATH;
    ccfg.tls.private_key_file        = CLIENT_PK_PATH;
    ccfg.tls.trusted_certificate_file = CA_CHAIN_PATH;

    socket_t srv, cli, acc;
    assert_int_equal(socket_init(&srv, &scfg), ok);

    result_t ct, st;
    drive_tls_session(NULL, &srv, &ccfg, &cli, &acc, &ct, &st);
    /* Server side must fail because the pin does not match. */
    assert_int_not_equal(st, ok);

    assert_int_equal(socket_deinit(&cli), ok);
    assert_int_equal(socket_deinit(&acc), ok);
    assert_int_equal(socket_deinit(&srv), ok);
}

/* ─────────────────────────────────────────────────────────────────────── *
 * Suite registration
 * ─────────────────────────────────────────────────────────────────────── */

int test_socket_features(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(default_helpers_set_nodelay_and_epoll),
        cmocka_unit_test(new_result_codes_are_distinct),
        cmocka_unit_test(buf_pool_zero_args_returns_null),
        cmocka_unit_test(buf_pool_acquire_release_round_trip),
        cmocka_unit_test(tcp_knobs_applied_to_listen_socket),
        cmocka_unit_test(reuse_port_allows_dual_bind),
        cmocka_unit_test(accept_nb_returns_nonblocking_fd),
        cmocka_unit_test(writev_nb_plain_two_segments),
        cmocka_unit_test(writev_nb_argument_guards),
        cmocka_unit_test(splice_rejects_tls_sockets),
        cmocka_unit_test(splice_plain_tcp_proxy_payload),
        cmocka_unit_test(tls13_only_handshake_succeeds),
        cmocka_unit_test(cipher_suites_and_curves_handshake_succeeds),
        cmocka_unit_test(require_peer_cert_rejects_unauthenticated_client),
        cmocka_unit_test(require_peer_cert_accepts_authenticated_client),
        cmocka_unit_test(spki_pin_match_succeeds),
        cmocka_unit_test(spki_pin_mismatch_rejects),
    };
    /* Suppress the "drive_handshake_pair unused" warning (kept around
     * for symmetry with future bidirectional tests). */
    (void)drive_handshake_pair;
    return cmocka_run_group_tests_name("socket_features", tests, NULL, NULL);
}
