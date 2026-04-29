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

static result_t fail_async(void* user_args, task_t* self)
{
    (void)user_args;
    (void)self;
    return error;
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

static void task_run_null_function_returns_null(void** state)
{
    (void)state;
    assert_null(task_run(NULL, NULL));
}

static void task_get_status_after_completion_success(void** state)
{
    (void)state;

    int counter = 0;
    task_t* task = task_run(counter_add_1_async, &counter);
    assert_non_null(task);

    assert_true(task_wait(task));
    assert_int_equal(task_status_ran_to_completion, task_get_status(task));
    assert_false(task_is_canceled(task));
    assert_false(task_is_faulted(task));

    task_release(task);
}

static void task_faulted_when_function_returns_error(void** state)
{
    (void)state;

    task_t* task = task_run(fail_async, NULL);
    assert_non_null(task);

    assert_true(task_wait(task));
    assert_true(task_is_faulted(task));
    assert_true(task_is_completed(task));
    assert_int_equal(task_status_faulted, task_get_status(task));
    assert_int_not_equal(ok, task_get_result(task));

    task_release(task);
}

static void continuation_callback(struct task* completed, void* state)
{
    (void)completed;
    int* invoked = (int*)state;
    (*invoked)++;
}

static void task_continuation_invoked_after_completion(void** state)
{
    (void)state;

    int counter = 0;
    int invoked = 0;
    task_t* task = task_run(counter_add_1_async, &counter);
    assert_non_null(task);

    assert_int_equal(ok, task_continue_with(task, continuation_callback, &invoked));
    assert_true(task_wait(task));

    /* Continuation runs on the worker thread before final release of its ref;
     * by the time wait returns the continuation has already run. */
    assert_int_equal(1, invoked);

    task_release(task);
}

static void task_wait_timeout_success(void** state)
{
    (void)state;

    int counter = 0;
    task_t* task = task_run(counter_add_1_async, &counter);
    assert_non_null(task);

    assert_true(task_wait_timeout(task, 5000));
    task_release(task);
}

static void task_fire_and_forget_release_before_wait(void** state)
{
    (void)state;

    int counter = 0;
    task_t* task = task_run(counter_add_1_async, &counter);
    assert_non_null(task);

    /* Fire-and-forget: release immediately. Worker still owns its reference
     * so the slot is freed when the function returns. */
    task_release(task);

    /* Give the worker time to finish. */
    sleep(1);
}

static void tcs_set_then_wait_success(void** state)
{
    (void)state;

    task_completion_source_t tcs;
    assert_int_equal(ok, task_completion_source_init(&tcs));

    assert_true(task_completion_source_set_result(&tcs, ok));
    assert_true(task_completion_source_is_completed(&tcs));
    assert_int_equal(ok, task_completion_source_wait(&tcs));

    assert_int_equal(ok, task_completion_source_deinit(&tcs));
}

static void tcs_double_set_returns_false(void** state)
{
    (void)state;

    task_completion_source_t tcs;
    assert_int_equal(ok, task_completion_source_init(&tcs));

    assert_true(task_completion_source_set_result(&tcs, ok));
    assert_false(task_completion_source_set_result(&tcs, error));

    /* First win sticks. */
    assert_int_equal(ok, task_completion_source_wait(&tcs));

    assert_int_equal(ok, task_completion_source_deinit(&tcs));
}

static void tcs_wait_timeout_returns_false(void** state)
{
    (void)state;

    task_completion_source_t tcs;
    assert_int_equal(ok, task_completion_source_init(&tcs));

    result_t r = error;   /* sentinel - must be left unchanged on timeout */
    assert_false(task_completion_source_wait_timeout(&tcs, 50, &r));

    assert_int_equal(ok, task_completion_source_deinit(&tcs));
}

static void tcs_is_completed_reports_state(void** state)
{
    (void)state;

    task_completion_source_t tcs;
    assert_int_equal(ok, task_completion_source_init(&tcs));

    assert_false(task_completion_source_is_completed(&tcs));
    assert_true(task_completion_source_set_result(&tcs, ok));
    assert_true(task_completion_source_is_completed(&tcs));

    assert_int_equal(ok, task_completion_source_deinit(&tcs));
}

int test_task()
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup_teardown(task_run_success,                            task_test_setup, task_test_teardown),
      cmocka_unit_test_setup_teardown(task_cancel_success,                         task_test_setup, task_test_teardown),
      cmocka_unit_test_setup_teardown(task_run_null_function_returns_null,         task_test_setup, task_test_teardown),
      cmocka_unit_test_setup_teardown(task_get_status_after_completion_success,    task_test_setup, task_test_teardown),
      cmocka_unit_test_setup_teardown(task_faulted_when_function_returns_error,    task_test_setup, task_test_teardown),
      cmocka_unit_test_setup_teardown(task_continuation_invoked_after_completion,  task_test_setup, task_test_teardown),
      cmocka_unit_test_setup_teardown(task_wait_timeout_success,                   task_test_setup, task_test_teardown),
      cmocka_unit_test_setup_teardown(task_fire_and_forget_release_before_wait,    task_test_setup, task_test_teardown),
      cmocka_unit_test_setup_teardown(tcs_set_then_wait_success,                   task_test_setup, task_test_teardown),
      cmocka_unit_test_setup_teardown(tcs_double_set_returns_false,                task_test_setup, task_test_teardown),
      cmocka_unit_test_setup_teardown(tcs_wait_timeout_returns_false,              task_test_setup, task_test_teardown),
      cmocka_unit_test_setup_teardown(tcs_is_completed_reports_state,              task_test_setup, task_test_teardown),
  };

  return cmocka_run_group_tests_name("task_tests", tests, NULL, NULL);
}
