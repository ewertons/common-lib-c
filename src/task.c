#include <stdlib.h>
#include <string.h>

#include "task.h"
#include "semaphore.h"

#define TASK_COUNT 10
static task_t tasks[TASK_COUNT];
static sem_t tasks_semaphore;

static task_t* reserve_task()
{
    task_t* task = NULL;

    if (sem_wait(&tasks_semaphore) == 0)
    {
        for(int i = 0; i < TASK_COUNT; i++)
        {
            if (!tasks[i].is_reserved)
            {
                tasks[i].is_reserved = true;
                task = &tasks[i];
                break;
            }
        }

        if (sem_post(&tasks_semaphore) != 0)
        {
            if (task != NULL)
            {
                task->is_reserved = false;
                task = NULL;
            }
        }
    }

    return task;
}

static void release_task(task_t* task)
{
    if (sem_wait(&tasks_semaphore) == 0)
    {
        task->is_reserved = false;

        if (sem_post(&tasks_semaphore) != 0)
        {
            tasks->is_reserved = false;
        }
    }
}

static void* inner_thread_function(void* args)
{
    task_t* task = (task_t*)args;
    
    task->result = task->function(task->user_args, task);
  
    pthread_exit(NULL); 
}

void task_platform_init()
{
    for(int i = 0; i < TASK_COUNT; i++)
    {
        memset(&tasks[i], 0, sizeof(task_t));
    }

    (void)sem_init(&tasks_semaphore, 0, 1);
}

void task_platform_deinit()
{
    (void)sem_destroy(&tasks_semaphore);
}


task_t* task_run(task_function_t function, void* args)
{
    task_t* task = reserve_task();

    if (task != NULL)
    {
        task->user_args = args;
        task->function = function;

        if (sem_init(&task->semaphore, 0, 1) != 0)
        {
            release_task(task);
            task = NULL;
        }
        else if (pthread_create(&task->thread, NULL, inner_thread_function, task) != 0)
        {
            release_task(task);
        }
    }

    return task;
}

task_t* task_continue_with(task_t* initial_task, task_t* continuation_task)
{
    (void)initial_task;
    (void)continuation_task;
    return NULL;
}

bool task_is_cancelled(task_t* task)
{
    if (task == NULL)
    {
        return false;
    }
    else
    {
        bool is_cancelled = false;

        if (task_lock(task) == 0)
        {
            is_cancelled = task->is_cancelled;

            (void)task_unlock(task);
        }

        return is_cancelled;
    }
}

void task_cancel(task_t* task)
{
    if (task != NULL)
    {
        if (task_lock(task) == 0)
        {
            task->is_cancelled = true;

            (void)task_unlock(task);
        }
    }
}

bool task_wait(task_t* task)
{
    if (task == NULL)
    {
        return false;
    }
    else
    {
        if (pthread_join(task->thread, NULL) != 0)
        {
            return false;
        }
        else
        {
            return true;
        }
    }
}

bool task_wait_any(task_t** tasks, int count)
{
    (void)tasks;
    (void)count;
    return false;
}

bool task_wait_all(task_t** tasks, int count)
{
    (void)tasks;
    (void)count;
    return false;
}
