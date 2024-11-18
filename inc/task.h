#ifndef TASK_H
#define TASK_H

#include <stdbool.h>

#include "niceties.h"

#include "pthread.h"
#include "semaphore.h"

struct task;

typedef result_t (*task_function_t)(void* user_args, struct task* my_task);

typedef struct task
{
    bool is_reserved;
    bool is_cancelled;
    pthread_t thread;
    task_function_t function;
    void* user_args;
    sem_t semaphore;
    result_t result;
} task_t;

#define task_lock(t) sem_wait(&t->semaphore)
#define task_unlock(t) sem_post(&t->semaphore)

void task_platform_init();
void task_platform_deinit();

task_t* task_run(task_function_t function, void* args);

task_t* task_continue_with(task_t* initial_task, task_t* continuation_task);
bool task_is_cancelled(task_t* task);
void task_cancel(task_t* task);
bool task_wait(task_t* task);
bool task_wait_any(task_t** tasks, int count);
bool task_wait_all(task_t** tasks, int count);

#endif // TASK_H