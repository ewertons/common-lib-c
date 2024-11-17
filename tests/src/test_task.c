#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <setjmp.h>

#include <cmocka.h>

#include <unistd.h>

#include "tests.h"
#include "task.h"



static void counter_add_1_async(void* user_args, task_t* my_task)
{
  int* counter = (int*)user_args;

  if (!task_is_cancelled(my_task))
  {
    (void)task_lock(my_task);
    (*counter)++;
    (void)task_unlock(my_task);
  }
}

static void keep_running_async(void* user_args, task_t* my_task)
{
  bool* is_cancelled = (bool*)user_args;

  while(!task_is_cancelled(my_task))
  {
    sleep(1);
  }

  task_lock(my_task);
  *is_cancelled = true;
  task_unlock(my_task);
}

static void task_run_success(void** state)
{
    (void)state;

    int counter = 0;

    task_t* task = task_run(counter_add_1_async, &counter);

    task_wait(task);

    assert_int_equal(counter, 1);
}

static void task_cancel_success(void** state)
{
    (void)state;

    bool is_cancelled = false;

    task_t* task = task_run(keep_running_async, &is_cancelled);

    sleep(1);

    task_lock(task);
    assert_false(is_cancelled);
    task_unlock(task);
    assert_false(task_is_cancelled(task));

    task_cancel(task);

    sleep(1);

    task_lock(task);
    assert_true(is_cancelled);
    task_unlock(task);
    assert_true(task_is_cancelled(task));

    assert_true(task_wait(task));
}

int test_task()
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(task_run_success),
      cmocka_unit_test(task_cancel_success),
  };

  return cmocka_run_group_tests_name("task_tests", tests, NULL, NULL);
}
