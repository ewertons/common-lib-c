#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

#include "task.h"

/* ------------------------------------------------------------------------- *
 * Static task pool. Zero dynamic allocation.
 * ------------------------------------------------------------------------- */
static task_t           g_tasks[TASK_POOL_SIZE];
static pthread_mutex_t  g_pool_mutex;            /* protects in_use / ref_count */
static pthread_cond_t   g_completion_cond;       /* broadcast on every task completion */
static bool             g_platform_initialized = false;

/* ------------------------------------------------------------------------- */

static bool is_terminal_status(task_status_t s)
{
    return s == task_status_ran_to_completion ||
           s == task_status_canceled          ||
           s == task_status_faulted;
}

/* Decrements a task's reference count. Must NOT be called with the task
 * mutex held. Returns the slot to the pool when the count reaches zero. */
static void task_decrement_ref(task_t* task)
{
    bool free_slot = false;

    (void)pthread_mutex_lock(&g_pool_mutex);

    task->ref_count--;

    if (task->ref_count <= 0)
    {
        task->in_use = false;
        free_slot = true;
    }

    (void)pthread_mutex_unlock(&g_pool_mutex);

    /* The mutex/cond stay initialized for the lifetime of the process; they
     * get reused next time the slot is reserved. This keeps the API
     * allocation-free. */
    (void)free_slot;
}

/* ------------------------------------------------------------------------- *
 * Worker thread entrypoint.
 * ------------------------------------------------------------------------- */
static void* inner_thread_function(void* args)
{
    task_t* task = (task_t*)args;

    (void)pthread_mutex_lock(&task->mutex);
    task->status = task_status_running;
    (void)pthread_cond_broadcast(&task->cond);
    (void)pthread_mutex_unlock(&task->mutex);

    result_t r = task->function(task->state, task);

    /* Capture continuation under lock, then publish terminal state. */
    task_continuation_t continuation;
    void*               continuation_state;

    (void)pthread_mutex_lock(&task->mutex);

    task->result = r;

    if (r == cancelled || task->cancel_requested)
    {
        task->status = task_status_canceled;
    }
    else if (is_error(r))
    {
        task->status = task_status_faulted;
    }
    else
    {
        task->status = task_status_ran_to_completion;
    }

    continuation       = task->continuation;
    continuation_state = task->continuation_state;

    (void)pthread_cond_broadcast(&task->cond);
    (void)pthread_mutex_unlock(&task->mutex);

    /* Notify waiters of wait_any / wait_all. */
    (void)pthread_mutex_lock(&g_pool_mutex);
    (void)pthread_cond_broadcast(&g_completion_cond);
    (void)pthread_mutex_unlock(&g_pool_mutex);

    /* Run continuation outside of any lock. */
    if (continuation != NULL)
    {
        continuation(task, continuation_state);
    }

    /* Worker releases its reference. */
    task_decrement_ref(task);

    return NULL;
}

/* ------------------------------------------------------------------------- *
 * Platform init / deinit.
 * ------------------------------------------------------------------------- */
result_t task_platform_init(void)
{
    if (g_platform_initialized)
    {
        return ok;
    }

    if (pthread_mutex_init(&g_pool_mutex, NULL) != 0)
    {
        return error;
    }

    if (pthread_cond_init(&g_completion_cond, NULL) != 0)
    {
        (void)pthread_mutex_destroy(&g_pool_mutex);
        return error;
    }

    for (int i = 0; i < TASK_POOL_SIZE; i++)
    {
        (void)memset(&g_tasks[i], 0, sizeof(task_t));

        if (pthread_mutex_init(&g_tasks[i].mutex, NULL) != 0)
        {
            return error;
        }

        if (pthread_cond_init(&g_tasks[i].cond, NULL) != 0)
        {
            (void)pthread_mutex_destroy(&g_tasks[i].mutex);
            return error;
        }

        g_tasks[i].status = task_status_idle;
    }

    g_platform_initialized = true;
    return ok;
}

result_t task_platform_deinit(void)
{
    if (!g_platform_initialized)
    {
        return ok;
    }

    /* Wait until every in-flight task has reached a terminal status. We can
     * not pthread_join because workers were detached. We still process leaked
     * caller references gracefully: once the worker has terminated, the slot
     * may safely be reclaimed at process shutdown. */
    for (;;)
    {
        bool any_running = false;

        (void)pthread_mutex_lock(&g_pool_mutex);
        for (int i = 0; i < TASK_POOL_SIZE; i++)
        {
            if (!g_tasks[i].in_use)
            {
                continue;
            }
            (void)pthread_mutex_unlock(&g_pool_mutex);

            task_status_t s = task_get_status(&g_tasks[i]);
            if (!is_terminal_status(s))
            {
                any_running = true;
                (void)pthread_mutex_lock(&g_pool_mutex);
                break;
            }
            (void)pthread_mutex_lock(&g_pool_mutex);
        }
        (void)pthread_mutex_unlock(&g_pool_mutex);

        if (!any_running)
        {
            break;
        }
        task_sleep_ms(5);
    }

    for (int i = 0; i < TASK_POOL_SIZE; i++)
    {
        (void)pthread_cond_destroy(&g_tasks[i].cond);
        (void)pthread_mutex_destroy(&g_tasks[i].mutex);
        g_tasks[i].in_use    = false;
        g_tasks[i].ref_count = 0;
    }

    (void)pthread_cond_destroy(&g_completion_cond);
    (void)pthread_mutex_destroy(&g_pool_mutex);

    g_platform_initialized = false;
    return ok;
}

/* ------------------------------------------------------------------------- *
 * Slot reservation.
 * ------------------------------------------------------------------------- */
static task_t* reserve_task(void)
{
    task_t* task = NULL;

    (void)pthread_mutex_lock(&g_pool_mutex);

    for (int i = 0; i < TASK_POOL_SIZE; i++)
    {
        if (!g_tasks[i].in_use)
        {
            task = &g_tasks[i];
            task->in_use           = true;
            task->ref_count        = 2; /* caller + worker */
            task->cancel_requested = false;
            task->result           = ok;
            task->status           = task_status_pending;
            task->continuation     = NULL;
            task->continuation_state = NULL;
            task->function         = NULL;
            task->state            = NULL;
            break;
        }
    }

    (void)pthread_mutex_unlock(&g_pool_mutex);

    return task;
}

static void release_reservation(task_t* task)
{
    (void)pthread_mutex_lock(&g_pool_mutex);
    task->in_use    = false;
    task->ref_count = 0;
    task->status    = task_status_idle;
    (void)pthread_mutex_unlock(&g_pool_mutex);
}

/* ------------------------------------------------------------------------- *
 * Public API.
 * ------------------------------------------------------------------------- */
task_t* task_run(task_function_t function, void* state)
{
    if (!g_platform_initialized || function == NULL)
    {
        return NULL;
    }

    task_t* task = reserve_task();

    if (task == NULL)
    {
        return NULL;
    }

    task->function = function;
    task->state    = state;

    if (pthread_create(&task->thread, NULL, inner_thread_function, task) != 0)
    {
        release_reservation(task);
        return NULL;
    }

    /* Detach so the OS reaps the thread; we synchronize via cond/mutex. */
    (void)pthread_detach(task->thread);

    return task;
}

task_status_t task_get_status(task_t* task)
{
    if (task == NULL)
    {
        return task_status_idle;
    }

    task_status_t s;

    (void)pthread_mutex_lock(&task->mutex);
    s = task->status;
    (void)pthread_mutex_unlock(&task->mutex);

    return s;
}

bool task_is_completed(task_t* task)
{
    return is_terminal_status(task_get_status(task));
}

bool task_is_canceled(task_t* task)
{
    return task_get_status(task) == task_status_canceled;
}

bool task_is_faulted(task_t* task)
{
    return task_get_status(task) == task_status_faulted;
}

result_t task_get_result(task_t* task)
{
    if (task == NULL)
    {
        return invalid_argument;
    }

    result_t r;

    (void)pthread_mutex_lock(&task->mutex);
    r = task->result;
    (void)pthread_mutex_unlock(&task->mutex);

    return r;
}

void task_request_cancel(task_t* task)
{
    if (task == NULL)
    {
        return;
    }

    (void)pthread_mutex_lock(&task->mutex);
    task->cancel_requested = true;
    (void)pthread_cond_broadcast(&task->cond);
    (void)pthread_mutex_unlock(&task->mutex);
}

bool task_is_cancellation_requested(task_t* task)
{
    if (task == NULL)
    {
        return false;
    }

    bool requested;

    (void)pthread_mutex_lock(&task->mutex);
    requested = task->cancel_requested;
    (void)pthread_mutex_unlock(&task->mutex);

    return requested;
}

bool task_wait(task_t* task)
{
    if (task == NULL)
    {
        return false;
    }

    (void)pthread_mutex_lock(&task->mutex);
    while (!is_terminal_status(task->status))
    {
        (void)pthread_cond_wait(&task->cond, &task->mutex);
    }
    (void)pthread_mutex_unlock(&task->mutex);

    return true;
}

bool task_wait_timeout(task_t* task, uint32_t timeout_ms)
{
    if (task == NULL)
    {
        return false;
    }

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec  += timeout_ms / 1000U;
    ts.tv_nsec += (long)((timeout_ms % 1000U) * 1000000UL);
    if (ts.tv_nsec >= 1000000000L)
    {
        ts.tv_sec  += 1;
        ts.tv_nsec -= 1000000000L;
    }

    bool done = false;
    int  rc   = 0;

    (void)pthread_mutex_lock(&task->mutex);
    while (!is_terminal_status(task->status) && rc == 0)
    {
        rc = pthread_cond_timedwait(&task->cond, &task->mutex, &ts);
    }
    done = is_terminal_status(task->status);
    (void)pthread_mutex_unlock(&task->mutex);

    return done;
}

int task_wait_any(task_t** tasks, int count)
{
    if (tasks == NULL || count <= 0)
    {
        return -1;
    }

    /* Quick non-blocking pass first. */
    for (int i = 0; i < count; i++)
    {
        if (tasks[i] != NULL && task_is_completed(tasks[i]))
        {
            return i;
        }
    }

    /* Block on the global completion cond, rechecking each wake. */
    (void)pthread_mutex_lock(&g_pool_mutex);
    for (;;)
    {
        for (int i = 0; i < count; i++)
        {
            if (tasks[i] == NULL)
            {
                continue;
            }
            /* Read status under the task mutex. We can't hold pool_mutex
             * while taking task->mutex (avoid lock ordering issues), so
             * release first. */
            (void)pthread_mutex_unlock(&g_pool_mutex);
            bool done = task_is_completed(tasks[i]);
            (void)pthread_mutex_lock(&g_pool_mutex);

            if (done)
            {
                (void)pthread_mutex_unlock(&g_pool_mutex);
                return i;
            }
        }
        (void)pthread_cond_wait(&g_completion_cond, &g_pool_mutex);
    }
}

bool task_wait_all(task_t** tasks, int count)
{
    if (tasks == NULL || count <= 0)
    {
        return false;
    }

    for (int i = 0; i < count; i++)
    {
        if (tasks[i] == NULL)
        {
            return false;
        }
        if (!task_wait(tasks[i]))
        {
            return false;
        }
    }
    return true;
}

void task_release(task_t* task)
{
    if (task == NULL)
    {
        return;
    }
    task_decrement_ref(task);
}

result_t task_continue_with(task_t* task, task_continuation_t continuation, void* state)
{
    if (task == NULL || continuation == NULL)
    {
        return invalid_argument;
    }

    result_t r;

    (void)pthread_mutex_lock(&task->mutex);

    if (task->continuation != NULL || is_terminal_status(task->status))
    {
        r = invalid_argument;
    }
    else
    {
        task->continuation       = continuation;
        task->continuation_state = state;
        r = ok;
    }

    (void)pthread_mutex_unlock(&task->mutex);

    return r;
}

void task_sleep_ms(uint32_t ms)
{
    struct timespec ts;
    ts.tv_sec  = (time_t)(ms / 1000U);
    ts.tv_nsec = (long)((ms % 1000U) * 1000000UL);

    while (nanosleep(&ts, &ts) == -1 && errno == EINTR)
    {
        /* retry */
    }
}
