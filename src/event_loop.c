#include "event_loop.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <sys/epoll.h>
#include <sys/eventfd.h>

#include "logging_simple.h"

/* ------------------------------------------------------------------------- *
 *                                Internals
 * ------------------------------------------------------------------------- */

static event_loop_entry_t* find_entry(event_loop_t* loop, int fd)
{
    for (int i = 0; i < EVENT_LOOP_MAX_FDS; i++)
    {
        if (loop->table[i].fd == fd)
        {
            return &loop->table[i];
        }
    }
    return NULL;
}

static event_loop_entry_t* alloc_entry(event_loop_t* loop)
{
    for (int i = 0; i < EVENT_LOOP_MAX_FDS; i++)
    {
        if (loop->table[i].fd == -1)
        {
            return &loop->table[i];
        }
    }
    return NULL;
}

static uint32_t to_epoll_events(uint32_t events)
{
    uint32_t e = 0;
    if (events & event_loop_event_read)  e |= EPOLLIN | EPOLLRDHUP;
    if (events & event_loop_event_write) e |= EPOLLOUT;
    /* EPOLLERR / EPOLLHUP are always delivered by epoll. */
    return e;
}

static uint32_t from_epoll_events(uint32_t epev)
{
    uint32_t e = 0;
    if (epev & (EPOLLIN  | EPOLLPRI))            e |= event_loop_event_read;
    if (epev & EPOLLOUT)                          e |= event_loop_event_write;
    if (epev & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) e |= event_loop_event_error;
    return e;
}

/* ------------------------------------------------------------------------- *
 *                                  API
 * ------------------------------------------------------------------------- */

result_t event_loop_init(event_loop_t* loop)
{
    if (loop == NULL)
    {
        return invalid_argument;
    }

    (void)memset(loop, 0, sizeof(*loop));

    for (int i = 0; i < EVENT_LOOP_MAX_FDS; i++)
    {
        loop->table[i].fd = -1;
    }

    if (pthread_mutex_init(&loop->table_mutex, NULL) != 0)
    {
        return error;
    }

    loop->epfd = epoll_create1(EPOLL_CLOEXEC);
    if (loop->epfd == -1)
    {
        log_error("epoll_create1 (%s)", strerror(errno));
        (void)pthread_mutex_destroy(&loop->table_mutex);
        return error;
    }

    loop->wakefd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (loop->wakefd == -1)
    {
        log_error("eventfd (%s)", strerror(errno));
        (void)close(loop->epfd);
        (void)pthread_mutex_destroy(&loop->table_mutex);
        return error;
    }

    /* Register the wake-up fd as a sentinel; its callback drains the
     * eventfd counter and re-checks the stop flag. */
    struct epoll_event ev = { 0 };
    ev.events  = EPOLLIN;
    ev.data.fd = loop->wakefd;
    if (epoll_ctl(loop->epfd, EPOLL_CTL_ADD, loop->wakefd, &ev) == -1)
    {
        log_error("epoll_ctl ADD wakefd (%s)", strerror(errno));
        (void)close(loop->wakefd);
        (void)close(loop->epfd);
        (void)pthread_mutex_destroy(&loop->table_mutex);
        return error;
    }

    return ok;
}

result_t event_loop_deinit(event_loop_t* loop)
{
    if (loop == NULL)
    {
        return invalid_argument;
    }

    if (loop->wakefd != -1)
    {
        (void)close(loop->wakefd);
        loop->wakefd = -1;
    }
    if (loop->epfd != -1)
    {
        (void)close(loop->epfd);
        loop->epfd = -1;
    }
    (void)pthread_mutex_destroy(&loop->table_mutex);
    return ok;
}

result_t event_loop_register(event_loop_t* loop,
                             int fd,
                             uint32_t events,
                             event_loop_callback_t cb,
                             void* user)
{
    if (loop == NULL || fd < 0 || cb == NULL)
    {
        return invalid_argument;
    }

    (void)pthread_mutex_lock(&loop->table_mutex);
    event_loop_entry_t* entry = alloc_entry(loop);
    if (entry == NULL)
    {
        (void)pthread_mutex_unlock(&loop->table_mutex);
        return insufficient_size;
    }
    entry->fd     = fd;
    entry->events = events;
    entry->cb     = cb;
    entry->user   = user;
    (void)pthread_mutex_unlock(&loop->table_mutex);

    struct epoll_event ev = { 0 };
    ev.events   = to_epoll_events(events);
    ev.data.fd  = fd;
    if (epoll_ctl(loop->epfd, EPOLL_CTL_ADD, fd, &ev) == -1)
    {
        log_error("epoll_ctl ADD fd=%d (%s)", fd, strerror(errno));
        (void)pthread_mutex_lock(&loop->table_mutex);
        entry->fd = -1;
        (void)pthread_mutex_unlock(&loop->table_mutex);
        return error;
    }
    return ok;
}

result_t event_loop_modify(event_loop_t* loop, int fd, uint32_t events)
{
    if (loop == NULL || fd < 0)
    {
        return invalid_argument;
    }

    (void)pthread_mutex_lock(&loop->table_mutex);
    event_loop_entry_t* entry = find_entry(loop, fd);
    if (entry == NULL)
    {
        (void)pthread_mutex_unlock(&loop->table_mutex);
        return not_found;
    }
    entry->events = events;
    (void)pthread_mutex_unlock(&loop->table_mutex);

    struct epoll_event ev = { 0 };
    ev.events  = to_epoll_events(events);
    ev.data.fd = fd;
    if (epoll_ctl(loop->epfd, EPOLL_CTL_MOD, fd, &ev) == -1)
    {
        log_error("epoll_ctl MOD fd=%d (%s)", fd, strerror(errno));
        return error;
    }
    return ok;
}

result_t event_loop_unregister(event_loop_t* loop, int fd)
{
    if (loop == NULL || fd < 0)
    {
        return invalid_argument;
    }

    (void)epoll_ctl(loop->epfd, EPOLL_CTL_DEL, fd, NULL);

    (void)pthread_mutex_lock(&loop->table_mutex);
    event_loop_entry_t* entry = find_entry(loop, fd);
    if (entry != NULL)
    {
        entry->fd = -1;
        entry->cb = NULL;
    }
    (void)pthread_mutex_unlock(&loop->table_mutex);
    return ok;
}

result_t event_loop_run_once(event_loop_t* loop, int timeout_ms)
{
    if (loop == NULL)
    {
        return invalid_argument;
    }

    struct epoll_event events[EVENT_LOOP_BATCH];
    int n = epoll_wait(loop->epfd, events, EVENT_LOOP_BATCH, timeout_ms);

    if (n == -1)
    {
        if (errno == EINTR)
        {
            return ok;
        }
        log_error("epoll_wait (%s)", strerror(errno));
        return error;
    }

    for (int i = 0; i < n; i++)
    {
        int fd = events[i].data.fd;

        if (fd == loop->wakefd)
        {
            uint64_t drain;
            (void)read(loop->wakefd, &drain, sizeof(drain));
            continue;
        }

        /* Snapshot the entry under the lock, then drop it before the
         * callback so the callback may freely call register/modify/
         * unregister. The callback's `user` pointer is owned by the
         * caller. */
        (void)pthread_mutex_lock(&loop->table_mutex);
        event_loop_entry_t* entry = find_entry(loop, fd);
        event_loop_callback_t cb = NULL;
        void* user = NULL;
        if (entry != NULL)
        {
            cb   = entry->cb;
            user = entry->user;
        }
        (void)pthread_mutex_unlock(&loop->table_mutex);

        if (cb != NULL)
        {
            cb(fd, from_epoll_events(events[i].events), user);
        }
    }

    return ok;
}

result_t event_loop_run(event_loop_t* loop)
{
    if (loop == NULL)
    {
        return invalid_argument;
    }

    loop->running        = true;
    loop->stop_requested = false;

    while (!loop->stop_requested)
    {
        result_t r = event_loop_run_once(loop, -1);
        if (is_error(r))
        {
            loop->running = false;
            return r;
        }
    }

    loop->running = false;
    return ok;
}

result_t event_loop_stop(event_loop_t* loop)
{
    if (loop == NULL)
    {
        return invalid_argument;
    }
    loop->stop_requested = true;

    /* Wake the loop in case it's blocked in epoll_wait. */
    if (loop->wakefd != -1)
    {
        uint64_t one = 1;
        (void)write(loop->wakefd, &one, sizeof(one));
    }
    return ok;
}
