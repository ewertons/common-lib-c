#ifndef SOCKET_H
#define SOCKET_H

#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "span.h"
#include "niceties.h"
#include "task.h"

#include <arpa/inet.h>
#include <sys/socket.h>

/* ------------------------------------------------------------------------- *
 * TLS backend abstraction.
 *
 * The header intentionally does NOT pull in <openssl/ssl.h> (or any other
 * TLS implementation header). All implementation-specific state lives in
 * an opaque #tls_backend_t struct whose definition is private to the
 * backend translation unit (e.g. src/socket.c for the OpenSSL backend, or
 * a future src/socket_mbedtls.c).
 *
 * Consequences:
 *   - Consumers of socket.h compile without an OpenSSL/mbedTLS toolchain
 *     in their include path.
 *   - The library can be compiled into either a static archive linked
 *     against a specific TLS backend, or a shared object that selects a
 *     backend at link time -- the ABI exposed by socket.h is the same
 *     either way because the only TLS-related field on the public type
 *     is a `void*`-sized opaque pointer.
 *
 * Backend selection (build-time):
 *   - SOCKET_TLS_OPENSSL  -- default; links against libssl/libcrypto.
 *   - SOCKET_TLS_MBEDTLS  -- (future) drop-in replacement.
 *   - SOCKET_TLS_NONE     -- (future) plain-TCP-only build, no TLS at
 *                            all; tls.enabled=true returns error.
 * ------------------------------------------------------------------------- */
typedef struct tls_backend tls_backend_t;

#define DEFAULT_LISTENING_PORT 8234

typedef enum socket_role
{
    socket_role_client,
    socket_role_server
} socket_role_t;

/* ------------------------------------------------------------------------- *
 * I/O model selector (§3.1 P1).
 *
 * The default `socket_io_model_epoll` keeps the existing non-blocking
 * API working unchanged.  `socket_io_model_io_uring` requires Linux
 * kernel >= 5.1 and liburing; the implementation links it under the
 * SOCKET_IO_URING build guard and stores backend state in the opaque
 * `socket_t::uring_ctx` pointer so consumers do not need <liburing.h>.
 * ------------------------------------------------------------------------- */
typedef enum socket_io_model
{
    socket_io_model_epoll    = 0, /* default, existing behaviour          */
    socket_io_model_io_uring = 1, /* requires kernel >= 5.1 + liburing    */
} socket_io_model_t;

/* ------------------------------------------------------------------------- *
 * Local (listener) endpoint configuration.
 *
 *  port           : TCP port to bind. 0 = ephemeral / OS-assigned.
 *  reuse_port     : Set SO_REUSEPORT before bind() so that several
 *                   threads/processes may listen on the same port.
 *                   Defaults to false (zero-init = disabled).
 *  interface_name : Optional NIC name passed to SO_BINDTODEVICE.
 *                   NULL = bind on every interface (existing behaviour).
 * ------------------------------------------------------------------------- */
typedef struct local_host_config
{
    int         port;
    bool        reuse_port;
    const char* interface_name;
} local_host_config_t;

typedef struct remote_host_config
{
    span_t hostname;
    int    port;
} remote_host_config_t;

/* ------------------------------------------------------------------------- *
 * Full socket configuration.
 *
 * All new fields are zero-safe: a `(socket_config_t){0}` (or the existing
 * default-config helpers) yields the legacy behaviour.  The TCP, TLS,
 * I/O-model and ACL groups are described by their member documentation
 * below.
 * ------------------------------------------------------------------------- */
typedef struct socket_config
{
    socket_role_t role;

    local_host_config_t  local;
    remote_host_config_t remote;

    /* ── TCP tuning knobs (§3.1 P3) ───────────────────────────────────── */
    struct
    {
        bool nodelay;            /* TCP_NODELAY -- disable Nagle.
                                    Default false in zero-init; the
                                    socket_get_default_*_config helpers
                                    set it to true (critical for SSH /
                                    interactive latency).               */
        bool quickack;           /* TCP_QUICKACK; default false.        */
        int  send_buf_size;      /* SO_SNDBUF; 0 = OS default.          */
        int  recv_buf_size;      /* SO_RCVBUF; 0 = OS default.          */
        int  keepalive_idle_sec; /* TCP_KEEPIDLE; 0 = keepalive off.    */
    } tcp;

    /* ── TLS configuration (§3.1, §3.2 S1-S3) ─────────────────────────── */
    struct
    {
        bool        enable;
        bool        tls13_only;        /* refuse TLS < 1.3.            */
        bool        require_peer_cert; /* enforce mTLS; sets
                                          SSL_VERIFY_FAIL_IF_NO_PEER_CERT */
        const char* certificate_file;
        const char* private_key_file;
        const char* trusted_certificate_file;
        const char* cipher_suites;     /* TLS 1.3 ciphersuite list.    */
        const char* curves;            /* e.g. "X25519:P-256".          */
        const uint8_t* pinned_spki_sha256; /* 32-byte SHA-256 of peer
                                              SPKI; NULL = no pinning. */
        int   (*verify_peer_cb)(void* cert, void* userdata); /* optional */
        void*   verify_peer_userdata;
    } tls;

    /* ── I/O model (§3.1 P1) ──────────────────────────────────────────── */
    socket_io_model_t io_model;

    /* ── Connection ACL (§3.2 S5) — server role only ──────────────────── */
    struct
    {
        const struct in_addr* allow_list;      /* NULL = allow all.    */
        size_t                allow_cnt;
        uint32_t              max_conns;        /* 0 = unlimited.      */
        uint32_t              max_conns_per_ip; /* 0 = unlimited.      */
    } acl;
} socket_config_t;

/* ------------------------------------------------------------------------- *
 * Runtime socket object.
 *
 * Field ordering follows §5 of the improvement plan.  `uring_ctx` is an
 * opaque pointer so the public header does not depend on <liburing.h>;
 * it is non-NULL only when `io_model == socket_io_model_io_uring`.
 * ------------------------------------------------------------------------- */
typedef struct socket
{
    socket_role_t        role;
    local_host_config_t  local;
    remote_host_config_t remote;

    int       listen_sd;
    int       sd;
    struct sockaddr_in sa_serv;
    struct sockaddr_in sa_cli;
    socklen_t client_len;

    /* Non-blocking driver state. `io_want` is the bitmask of
     * #socket_io_want_t the last NB operation reported. `tcp_connected`
     * indicates the connect() phase finished for clients. */
    uint32_t io_want;
    bool     tcp_connected;

    /* All TLS-related state, grouped together so consumers can reason
     * about it as one unit (`s.tls.enabled`, `s.tls.handshake_done`).
     * The `backend` pointer is opaque and owned by whichever TLS backend
     * is linked into the build; treat it as private to socket.c. */
    struct
    {
        bool           enabled;
        bool           handshake_done;
        tls_backend_t* backend;
    } tls;

    /* io_uring context, opaque to consumers (private to socket.c).
     * NULL when io_model == socket_io_model_epoll. */
    void* uring_ctx;

    struct socket* parent;
} socket_t;

static inline socket_config_t socket_get_default_secure_server_config(void)
{
    socket_config_t config = { 0 };
    config.role          = socket_role_server;
    config.tls.enable    = true;
    config.local.port    = DEFAULT_LISTENING_PORT;
    config.tcp.nodelay   = true; /* critical for SSH interactive latency */
    config.io_model      = socket_io_model_epoll;
    return config;
}

static inline socket_config_t socket_get_default_secure_client_config(void)
{
    socket_config_t config = { 0 };
    config.role          = socket_role_client;
    config.tls.enable    = true;
    config.local.port    = 0;
    config.tcp.nodelay   = true;
    config.io_model      = socket_io_model_epoll;
    return config;
}

/* Plain (no-TLS) variants. Identical to the secure helpers above except
 * that tls.enable is false; useful for embedded testing or HTTP-only
 * deployments. */
static inline socket_config_t socket_get_default_plain_server_config(void)
{
    socket_config_t config = { 0 };
    config.role          = socket_role_server;
    config.tls.enable    = false;
    config.local.port    = DEFAULT_LISTENING_PORT;
    config.tcp.nodelay   = true;
    config.io_model      = socket_io_model_epoll;
    return config;
}

static inline socket_config_t socket_get_default_plain_client_config(void)
{
    socket_config_t config = { 0 };
    config.role          = socket_role_client;
    config.tls.enable    = false;
    config.local.port    = 0;
    config.tcp.nodelay   = true;
    config.io_model      = socket_io_model_epoll;
    return config;
}

result_t socket_init(socket_t* socket, socket_config_t* config);
result_t socket_deinit(socket_t* socket);

/* ------------------------------------------------------------------------- *
 * Blocking API.
 *
 * Intended for clients, simple utilities, and tests where multiplexing
 * many connections from a single thread is not required. Each call
 * runs to completion (or error) before returning. The blocking API is
 * also what the #stream_t adapter in `socket_stream.c` is built on, so
 * higher-level code that consumes a #stream_t (e.g. http-c's client-side
 * request/response path) implicitly uses these.
 * ------------------------------------------------------------------------- */
result_t socket_accept (socket_t* server, socket_t* client);
task_t*  socket_accept_async(socket_t* server, socket_t* client);
result_t socket_connect(socket_t* client);
result_t socket_read   (socket_t* socket, span_t buffer, span_t* out_read, span_t* remainder);
result_t socket_write  (socket_t* socket, span_t data);

/* ------------------------------------------------------------------------- *
 * Non-blocking API.
 *
 * Intended for event-loop driven servers (and clients) that multiplex
 * many file descriptors from a single thread. The caller drives
 * readiness via an external event loop and retries on `try_again`.
 *
 * After a successful `socket_init` for a server role, call
 * `socket_set_nonblocking(server->listen_sd)`. Then for each client:
 *   1. socket_accept_nb(server, &client)         -> try_again | ok | error
 *      (uses accept4(SOCK_NONBLOCK | SOCK_CLOEXEC) internally; no
 *       separate socket_set_nonblocking call required for the client fd).
 *   2. loop: socket_handshake_nb(&client)        -> try_again | ok | error
 *   3. socket_read_nb / socket_write_nb          -> try_again | ok | error
 *
 * For a client role, after `socket_init`:
 *   1. socket_connect_nb_begin(&client)          -> ok (TCP in progress) | error
 *   2. loop: socket_connect_nb_continue(&client) -> try_again | ok | error
 *   3. loop: socket_handshake_nb(&client)        -> try_again | ok | error
 *
 * `socket_get_io_want` exposes whether the next operation needs the fd
 * to become readable, writable, or both, so the caller can re-arm the
 * event loop accordingly.
 * ------------------------------------------------------------------------- */

typedef enum socket_io_want
{
    socket_io_want_none  = 0,
    socket_io_want_read  = 1 << 0,
    socket_io_want_write = 1 << 1,
} socket_io_want_t;

result_t socket_set_nonblocking(int fd);

result_t socket_accept_nb(socket_t* server, socket_t* client);
result_t socket_handshake_nb(socket_t* socket);
uint32_t socket_get_io_want(socket_t* socket);

result_t socket_connect_nb_begin(socket_t* client);
result_t socket_connect_nb_continue(socket_t* client);

/**
 * @brief Non-blocking write. Writes as many bytes as possible and reports
 *        the number actually accepted in `*out_written`.
 *
 * @return ok (all written), try_again (would block - caller must retry
 *         later, possibly with a smaller / advanced span), or error.
 */
result_t socket_write_nb(socket_t* socket, span_t data, uint32_t* out_written);

/**
 * @brief Non-blocking read. Reads up to `span_get_size(dst)` bytes from
 *        the connected socket into the buffer described by `dst` and
 *        stores the count actually received in `*out_received`.
 *        Transparently dispatches between the active TLS backend (when
 *        `tls.enabled`) and plain TCP (recv()).
 *
 * @return ok           Some bytes were read.
 *         try_again    No data ready; caller should re-arm and retry.
 *                      `*socket_get_io_want` reports the desired event mask.
 *         end_of_data  Peer closed the connection cleanly.
 *         error        Fatal I/O or TLS error.
 */
result_t socket_read_nb(socket_t* socket, span_t dst, uint32_t* out_received);

/**
 * @brief Vectored non-blocking write (writev / MSG_ZEROCOPY path).
 *
 * On Linux >= 4.14 the implementation SHOULD attempt MSG_ZEROCOPY when
 * @p iov_cnt == 1 and the socket is plain TCP (not TLS), falling back
 * to a normal sendmsg() on `ENOTSUP` / older kernels.
 *
 * @param iov     Array of spans to write in order.
 * @param iov_cnt Number of entries in @p iov.
 *
 * @return ok          All bytes accepted; *out_written = total size.
 *         try_again   Would block; *out_written = bytes accepted so far.
 *         error       Fatal I/O error.
 */
result_t socket_writev_nb(socket_t* socket,
                          const span_t* iov, size_t iov_cnt,
                          uint32_t* out_written);

/**
 * @brief Zero-copy splice between two plain TCP sockets (proxy fast path).
 *
 * Uses splice(2) to move up to @p max_bytes from @p src to @p dst without
 * passing data through user space.  Both sockets must be plain TCP
 * (`tls.enabled == false`).
 *
 * @return ok                          Bytes spliced; *out_spliced updated.
 *         try_again                   Would block on src or dst; caller
 *                                     re-arms its event loop.
 *         end_of_data                 src reached EOF.
 *         result_splice_unsupported   Either socket has TLS enabled.
 *         error                       Fatal I/O error.
 */
result_t socket_splice(socket_t* src, socket_t* dst,
                       size_t max_bytes, size_t* out_spliced);

/* ------------------------------------------------------------------------- *
 * Buffer pool (§3.1 P6).
 *
 * A simple slab allocator: `pool_count` fixed-size buffers are allocated
 * up-front and handed out via `socket_buf_pool_acquire`.  The pool is
 * optional -- existing call sites continue to use malloc/free unchanged.
 * The implementation uses a mutex + semaphore so `acquire` blocks until
 * a buffer is free.
 * ------------------------------------------------------------------------- */
typedef struct socket_buf_pool socket_buf_pool_t;

socket_buf_pool_t* socket_buf_pool_create(size_t buf_size, size_t pool_count);
void               socket_buf_pool_destroy(socket_buf_pool_t* pool);
void*              socket_buf_pool_acquire(socket_buf_pool_t* pool);
void               socket_buf_pool_release(socket_buf_pool_t* pool, void* buf);

#endif // SOCKET_H
