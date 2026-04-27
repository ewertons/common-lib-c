#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <setjmp.h>

#include <cmocka.h>

#include <unistd.h>

#include "tests.h"
#include "task.h"



static result_t counter_add_1_async(void* user_args, task_t* my_task)
{
  int* counter = (int*)user_args;

  if (!task_is_cancelled(my_task))
  {
    (void)task_lock(my_task);
    (*counter)++;
    (void)task_unlock(my_task);
  }

  return ok;
}

static result_t keep_running_async(void* user_args, task_t* my_task)
{
  bool* is_cancelled = (bool*)user_args;

  while(!task_is_cancelled(my_task))
  {
    sleep(1);
  }

  task_lock(my_task);
  *is_cancelled = true;
  task_unlock(my_task);

  return cancelled;
}

static int task_test_setup(void** state)
{
    (void)state;
    return task_platform_init() == ok ? 0 : -1;
}

static int task_test_teardown(void** state)
{
    (void)state;
    (void)task_platform_deinit();
    return 0;
}

static void task_run_success(void** state)
{
    (void)state;

    int counter = 0;

    task_t* task = task_run(counter_add_1_async, &counter);
    assert_non_null(task);

    assert_true(task_wait(task));

    assert_int_equal(counter, 1);
    assert_true(task_is_completed(task));
    assert_int_equal(task_get_result(task), ok);

    task_release(task);
}

static void task_cancel_success(void** state)
{
    (void)state;

    bool is_cancelled = false;

    task_t* task = task_run(keep_running_async, &is_cancelled);
    assert_non_null(task);

    sleep(1);

    task_lock(task);
    assert_false(is_cancelled);
    task_unlock(task);
    assert_false(task_is_cancelled(task));

    task_cancel(task);

    assert_true(task_wait(task));

    task_lock(task);
    assert_true(is_cancelled);
    task_unlock(task);
    assert_true(task_is_cancelled(task));
    assert_true(task_is_canceled(task));

    task_release(task);
}

int test_task()
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup_teardown(task_run_success,    task_test_setup, task_test_teardown),
      cmocka_unit_test_setup_teardown(task_cancel_success, task_test_setup, task_test_teardown),
  };

  return cmocka_run_group_tests_name("task_tests", tests, NULL, NULL);
}
