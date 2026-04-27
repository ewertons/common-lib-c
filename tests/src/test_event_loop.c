#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

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

int test_event_loop()
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(event_loop_run_once_pipe_read_succeed),
        cmocka_unit_test(event_loop_modify_and_unregister_succeed),
        cmocka_unit_test(event_loop_stop_from_other_thread_wakes_loop),
    };
    return cmocka_run_group_tests_name("event_loop", tests, NULL, NULL);
}
