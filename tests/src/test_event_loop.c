#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>

#include <cmocka.h>

#include <tests.h>
#include "event_loop.h"
#include "task.h"

typedef struct read_ctx
{
    event_loop_t* loop;
    int           fd;
    bool          fired;
    uint8_t       buffer[16];
    int           bytes;
} read_ctx_t;

static void read_callback(int fd, uint32_t events, void* user)
{
    read_ctx_t* ctx = (read_ctx_t*)user;
    if (events & event_loop_event_read)
    {
        ctx->bytes = (int)read(fd, ctx->buffer, sizeof(ctx->buffer));
        ctx->fired = true;
        (void)event_loop_unregister(ctx->loop, fd);
        (void)event_loop_stop(ctx->loop);
    }
}

static void event_loop_run_once_pipe_read_succeed(void** state)
{
    (void)state;

    int fds[2];
    assert_int_equal(pipe(fds), 0);
    /* Make the read end non-blocking — that's the contract. */
    int flags = fcntl(fds[0], F_GETFL, 0);
    assert_int_equal(fcntl(fds[0], F_SETFL, flags | O_NONBLOCK), 0);

    event_loop_t loop;
    assert_int_equal(event_loop_init(&loop), ok);

    read_ctx_t ctx = { 0 };
    ctx.loop = &loop;
    ctx.fd   = fds[0];

    assert_int_equal(event_loop_register(&loop, fds[0], event_loop_event_read,
                                         read_callback, &ctx), ok);

    /* Write data so the reader becomes ready. */
    static const char payload[] = "hello";
    assert_int_equal((int)write(fds[1], payload, sizeof(payload) - 1),
                     (int)(sizeof(payload) - 1));

    /* Drive the loop until the callback stops it. */
    assert_int_equal(event_loop_run_once(&loop, 1000), ok);

    assert_true(ctx.fired);
    assert_int_equal(ctx.bytes, (int)(sizeof(payload) - 1));
    assert_memory_equal(ctx.buffer, payload, sizeof(payload) - 1);

    (void)close(fds[0]);
    (void)close(fds[1]);
    assert_int_equal(event_loop_deinit(&loop), ok);
}

static void event_loop_modify_and_unregister_succeed(void** state)
{
    (void)state;
    event_loop_t loop;
    assert_int_equal(event_loop_init(&loop), ok);

    int fds[2];
    assert_int_equal(pipe(fds), 0);

    read_ctx_t ctx = { 0 };
    ctx.loop = &loop;

    assert_int_equal(event_loop_register(&loop, fds[0], event_loop_event_read,
                                         read_callback, &ctx), ok);
    assert_int_equal(event_loop_modify(&loop, fds[0],
                                       event_loop_event_read | event_loop_event_write),
                     ok);
    assert_int_equal(event_loop_unregister(&loop, fds[0]), ok);

    (void)close(fds[0]);
    (void)close(fds[1]);
    assert_int_equal(event_loop_deinit(&loop), ok);
}

static void event_loop_stop_from_other_thread_wakes_loop(void** state)
{
    (void)state;
    /* Just verify stop() returns ok and the wake fd consumes a write. */
    event_loop_t loop;
    assert_int_equal(event_loop_init(&loop), ok);
    assert_int_equal(event_loop_stop(&loop), ok);
    /* run_once with timeout=0 should return immediately, draining the wake fd. */
    assert_int_equal(event_loop_run_once(&loop, 0), ok);
    assert_int_equal(event_loop_deinit(&loop), ok);
}

static void event_loop_wake_rejects_null(void** state)
{
    (void)state;
    assert_int_equal(event_loop_wake(NULL), invalid_argument);
}

static void event_loop_wake_does_not_request_stop(void** state)
{
    (void)state;
    /* The whole point of wake(): it kicks the loop awake but must leave it
     * running. If it set stop_requested like stop() does, a cross-thread
     * completion would tear the server down instead of flushing a
     * response. */
    event_loop_t loop;
    assert_int_equal(event_loop_init(&loop), ok);

    assert_int_equal(event_loop_wake(&loop), ok);
    assert_false(loop.stop_requested);

    /* Draining the kick must not change that. */
    assert_int_equal(event_loop_run_once(&loop, 0), ok);
    assert_false(loop.stop_requested);

    assert_int_equal(event_loop_deinit(&loop), ok);
}

#if defined(EVENT_LOOP_BACKEND_EPOLL)

typedef struct wake_ctx
{
    event_loop_t* loop;
    unsigned      delay_us;
} wake_ctx_t;

static void* wake_after_delay(void* arg)
{
    wake_ctx_t* ctx = (wake_ctx_t*)arg;
    usleep(ctx->delay_us);
    (void)event_loop_wake(ctx->loop);
    return NULL;
}

static void event_loop_wake_from_other_thread_unblocks_run_once(void** state)
{
    (void)state;
    /* The property the deferred-response feature rests on: a worker thread
     * can pull the loop thread out of its wait. Guarded to epoll because
     * the select backend has no eventfd and instead relies on its bounded
     * wait interval -- documented behaviour, not a wake it can assert. */
    event_loop_t loop;
    assert_int_equal(event_loop_init(&loop), ok);

    wake_ctx_t ctx;
    ctx.loop     = &loop;
    ctx.delay_us = 50 * 1000;

    pthread_t thread;
    assert_int_equal(pthread_create(&thread, NULL, wake_after_delay, &ctx), 0);

    struct timespec t0;
    struct timespec t1;
    assert_int_equal(clock_gettime(CLOCK_MONOTONIC, &t0), 0);

    /* A finite ceiling rather than -1: if the wake never lands we fall out
     * on the timeout and fail on elapsed time with a useful number,
     * instead of hanging until ctest kills the run. */
    assert_int_equal(event_loop_run_once(&loop, 5000), ok);

    assert_int_equal(clock_gettime(CLOCK_MONOTONIC, &t1), 0);
    assert_int_equal(pthread_join(thread, NULL), 0);

    long elapsed_ms = (t1.tv_sec - t0.tv_sec) * 1000
                    + (t1.tv_nsec - t0.tv_nsec) / 1000000;
    assert_true(elapsed_ms < 2000);
    assert_false(loop.stop_requested);

    assert_int_equal(event_loop_deinit(&loop), ok);
}

#endif /* EVENT_LOOP_BACKEND_EPOLL */

int test_event_loop()
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(event_loop_run_once_pipe_read_succeed),
        cmocka_unit_test(event_loop_modify_and_unregister_succeed),
        cmocka_unit_test(event_loop_stop_from_other_thread_wakes_loop),
        cmocka_unit_test(event_loop_wake_rejects_null),
        cmocka_unit_test(event_loop_wake_does_not_request_stop),
#if defined(EVENT_LOOP_BACKEND_EPOLL)
        cmocka_unit_test(event_loop_wake_from_other_thread_unblocks_run_once),
#endif
    };
    return cmocka_run_group_tests_name("event_loop", tests, NULL, NULL);
}
