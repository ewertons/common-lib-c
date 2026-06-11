#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

/**
 * @file event_loop.h
 *
 * @brief Single-threaded readiness-based event loop abstraction.
 *
 * The interface is intentionally tiny and platform-neutral. Backends:
 *
 *   - epoll  (Linux, default)
 *   - select (portable: BSD, macOS, ESP-IDF / lwIP / FreeRTOS)
 *   - kqueue (BSD, future)
 *
 * The contract is "register a file descriptor with the events you care
 * about + a callback; run the loop; the loop invokes the callback when the
 * fd is ready." Callbacks must be non-blocking and may re-arm the
 * descriptor for different events.
 */

#include <stdbool.h>
#include <stdint.h>

#include "niceties.h"

#ifndef EVENT_LOOP_MAX_FDS
/* Hard cap on simultaneously-registered descriptors. The loop pre-allocates
 * a bookkeeping table of this size; no malloc per registration. */
#define EVENT_LOOP_MAX_FDS 1024
#endif

#ifndef EVENT_LOOP_BATCH
/* Maximum number of ready descriptors processed per epoll_wait. */
#define EVENT_LOOP_BATCH 64
#endif

typedef enum event_loop_event
{
    event_loop_event_none  = 0,
    event_loop_event_read  = 1 << 0, /* fd is readable */
    event_loop_event_write = 1 << 1, /* fd is writable */
    event_loop_event_error = 1 << 2, /* hangup / error (delivered always)  */
} event_loop_event_t;

typedef struct event_loop event_loop_t;

/**
 * @brief Callback invoked when a registered fd becomes ready.
 *
 * @param fd       The descriptor that became ready.
 * @param events   Bitmask of #event_loop_event_t describing readiness.
 * @param user     The opaque pointer supplied at registration time.
 *
 * The callback runs on the loop thread. It MUST NOT block. It may call any
 * `event_loop_*` function (re-arm, modify, unregister, stop).
 */
typedef void (*event_loop_callback_t)(int fd, uint32_t events, void* user);

result_t event_loop_init(event_loop_t* loop);
result_t event_loop_deinit(event_loop_t* loop);

/**
 * @brief Register `fd` with the loop. The descriptor must already be in
 *        non-blocking mode.
 */
result_t event_loop_register(event_loop_t* loop,
                             int fd,
                             uint32_t events,
                             event_loop_callback_t cb,
                             void* user);

/**
 * @brief Change the events of interest for an already-registered fd.
 *        The callback and user pointer are unchanged.
 */
result_t event_loop_modify(event_loop_t* loop, int fd, uint32_t events);

/**
 * @brief Remove `fd` from the loop. After this call the loop will not
 *        invoke the callback for that fd; the caller is responsible for
 *        closing the fd.
 */
result_t event_loop_unregister(event_loop_t* loop, int fd);

/**
 * @brief Run the loop until #event_loop_stop is called.
 *
 * Each iteration: wait up to `timeout_ms` for ready descriptors (or
 * forever if `timeout_ms < 0`), then invoke callbacks. Use
 * #event_loop_run_once for embedded scenarios that interleave with their
 * own scheduler.
 */
result_t event_loop_run(event_loop_t* loop);

/**
 * @brief Process at most one batch of ready descriptors. Useful for
 *        cooperative integration with another scheduler.
 *
 * @param timeout_ms  Negative for infinite, 0 for non-blocking poll.
 */
result_t event_loop_run_once(event_loop_t* loop, int timeout_ms);

/**
 * @brief Request the running loop to exit. Safe to call from any thread or
 *        from inside a callback.
 */
result_t event_loop_stop(event_loop_t* loop);

/* ------------------------------------------------------------------------- *
 *                            Backend selection
 *
 * The loop has two interchangeable readiness backends:
 *
 *   - epoll  : Linux only. Scales to thousands of descriptors and wakes
 *              instantly across threads via an eventfd.
 *   - select : Portable fallback used on every other platform (BSD, macOS,
 *              and -- crucially -- ESP-IDF / lwIP, which has no epoll). It
 *              relies only on the POSIX/BSD-socket `select()` available in
 *              lwIP and newlib, so no eventfd / pipe is required.
 *
 * Selection is automatic but can be forced at build time:
 *
 *   -DEVENT_LOOP_BACKEND_EPOLL=1   force epoll  (Linux only)
 *   -DEVENT_LOOP_BACKEND_SELECT=1  force select (any platform)
 * ------------------------------------------------------------------------- */
#if !defined(EVENT_LOOP_BACKEND_EPOLL) && !defined(EVENT_LOOP_BACKEND_SELECT)
#  if defined(__linux__)
#    define EVENT_LOOP_BACKEND_EPOLL 1
#  else
#    define EVENT_LOOP_BACKEND_SELECT 1
#  endif
#endif

#if defined(EVENT_LOOP_BACKEND_EPOLL) && defined(EVENT_LOOP_BACKEND_SELECT)
#  error "event_loop: choose only one of EPOLL / SELECT backends"
#endif

/* Concrete struct laid out here so the http_server can embed it without a
 * heap allocation. Treat fields as private. */

typedef struct event_loop_entry
{
    int                   fd;             /* -1 = free slot */
    uint32_t              events;
    event_loop_callback_t cb;
    void*                 user;
} event_loop_entry_t;

#if defined(EVENT_LOOP_BACKEND_EPOLL)

#include <pthread.h>

struct event_loop
{
    int                 epfd;
    int                 wakefd;            /* eventfd for stop / cross-thread wake */
    bool                running;
    bool                stop_requested;
    pthread_mutex_t     table_mutex;
    event_loop_entry_t  table[EVENT_LOOP_MAX_FDS];
};

#elif defined(EVENT_LOOP_BACKEND_SELECT)

#include <pthread.h>

#ifndef EVENT_LOOP_SELECT_WAKE_INTERVAL_MS
/* select() has no eventfd-style kick, so an infinite #event_loop_run wait is
 * implemented as a sequence of bounded waits. #event_loop_stop just flips a
 * flag; the loop observes it within (at most) this interval. Small enough to
 * feel instant, large enough to stay idle. */
#define EVENT_LOOP_SELECT_WAKE_INTERVAL_MS 50
#endif

struct event_loop
{
    bool                running;
    bool                stop_requested;
    pthread_mutex_t     table_mutex;
    event_loop_entry_t  table[EVENT_LOOP_MAX_FDS];
};

#else
#error "event_loop: no backend for this platform yet"
#endif

#endif /* EVENT_LOOP_H */
