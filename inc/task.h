#ifndef TASK_H
#define TASK_H

#include <stdbool.h>
#include <stdint.h>

#include <pthread.h>

#include "niceties.h"

/*
 * task.h - C# Task-like asynchronous task API for C99.
 *
 * Design:
 *   - Zero dynamic allocation. A fixed pool of #TASK_POOL_SIZE task slots is
 *     pre-allocated at process start time.
 *   - Each task has two reference holders: the caller that spawned it and the
 *     worker thread that runs it. The slot is returned to the pool when the
 *     last reference is released. Equivalent to the GC pinning model C# uses
 *     for #Task instances.
 *   - Cooperative cancellation. The function periodically checks
 *     #task_is_cancellation_requested() (analogous to
 *     CancellationToken.IsCancellationRequested). It returns the #cancelled
 *     result code to indicate the task ended due to cancellation.
 *   - One synchronous continuation per task. Continuations run on the worker
 *     thread immediately after the task function returns, before the worker's
 *     reference is released.
 *
 * Lifecycle (typical):
 *
 *     task_t* t = task_run(my_function, my_state);
 *     task_wait(t);                     // blocks until terminal state
 *     result_t r = task_get_result(t);  // observes outcome
 *     task_release(t);                  // returns the caller's reference
 *
 * Fire-and-forget:
 *
 *     task_t* t = task_run(my_function, my_state);
 *     if (t != NULL) task_release(t);   // worker still owns its reference
 */

#ifndef TASK_POOL_SIZE
#define TASK_POOL_SIZE 32
#endif

struct task;

typedef enum task_status
{
    task_status_idle = 0,            /* Slot not in use. */
    task_status_pending,             /* Reserved, worker not yet started. */
    task_status_running,             /* Function is executing. */
    task_status_ran_to_completion,   /* Function returned a success result. */
    task_status_canceled,            /* Task ended via cooperative cancellation. */
    task_status_faulted              /* Function returned an error result. */
} task_status_t;

typedef result_t (*task_function_t)(void* state, struct task* self);
typedef void     (*task_continuation_t)(struct task* completed_task, void* state);

typedef struct task
{
    /* --- Pool bookkeeping (protected by the global pool mutex) ---------- */
    bool in_use;
    int  ref_count;

    /* --- Threading primitives ------------------------------------------- */
    pthread_t       thread;
    pthread_mutex_t mutex;       /* protects the fields below */
    pthread_cond_t  cond;        /* broadcast on every status change */

    /* --- Work ----------------------------------------------------------- */
    task_function_t function;
    void*           state;

    /* --- State ---------------------------------------------------------- */
    task_status_t status;
    bool          cancel_requested;
    result_t      result;

    /* --- Single inline continuation ------------------------------------- */
    task_continuation_t continuation;
    void*               continuation_state;
} task_t;

/* ------------------------------------------------------------------------- *
 * Process-wide initialization. Must be called before any other task_*
 * function is used. task_platform_deinit() will block until every running
 * task reaches a terminal state.
 * ------------------------------------------------------------------------- */
result_t task_platform_init(void);
result_t task_platform_deinit(void);

/* ------------------------------------------------------------------------- *
 * Spawn a task. The returned handle is owned by the caller and must be
 * released via #task_release (or via #task_wait followed by #task_release).
 * Returns NULL when no slot is available or when the worker thread cannot be
 * started.
 * ------------------------------------------------------------------------- */
task_t* task_run(task_function_t function, void* state);

/* ------------------------------------------------------------------------- *
 * Status accessors. All are thread-safe.
 * ------------------------------------------------------------------------- */
task_status_t task_get_status(task_t* task);
bool          task_is_completed(task_t* task);  /* any terminal state */
bool          task_is_canceled(task_t* task);
bool          task_is_faulted(task_t* task);
result_t      task_get_result(task_t* task);

/* ------------------------------------------------------------------------- *
 * Cooperative cancellation.
 *   - task_request_cancel: signals the running task to stop.
 *   - task_is_cancellation_requested: meant to be polled from inside the
 *     task function (analogous to CancellationToken.IsCancellationRequested).
 * ------------------------------------------------------------------------- */
void task_request_cancel(task_t* task);
bool task_is_cancellation_requested(task_t* task);

/* ------------------------------------------------------------------------- *
 * Wait helpers. Returns false on invalid arguments or timeout.
 * Note: waiting does NOT release the caller's reference.
 * ------------------------------------------------------------------------- */
bool task_wait(task_t* task);
bool task_wait_timeout(task_t* task, uint32_t timeout_ms);
int  task_wait_any(task_t** tasks, int count);   /* returns index of a completed task, -1 on error */
bool task_wait_all(task_t** tasks, int count);

/* ------------------------------------------------------------------------- *
 * Release the caller's reference. After this call the handle must not be
 * used. The slot is returned to the pool when the last reference (worker
 * + caller) is released.
 * ------------------------------------------------------------------------- */
void task_release(task_t* task);

/* ------------------------------------------------------------------------- *
 * Schedule a continuation. The continuation is invoked on the worker thread
 * immediately after the task function returns. Only one continuation can be
 * registered. Returns #invalid_argument if a continuation is already set or
 * if the task has already completed.
 * ------------------------------------------------------------------------- */
result_t task_continue_with(task_t* task, task_continuation_t continuation, void* state);

/* ------------------------------------------------------------------------- *
 * Helpers.
 * ------------------------------------------------------------------------- */
void task_sleep_ms(uint32_t ms);

/* ------------------------------------------------------------------------- *
 * Backward-compatible aliases for the previous, simpler API.
 * ------------------------------------------------------------------------- */
#define task_is_cancelled(t)  task_is_cancellation_requested(t)
#define task_cancel(t)        task_request_cancel(t)
#define task_lock(t)          pthread_mutex_lock(&(t)->mutex)
#define task_unlock(t)        pthread_mutex_unlock(&(t)->mutex)

#endif /* TASK_H */