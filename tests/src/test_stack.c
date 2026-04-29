#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

#include "tests.h"
#include "stack.h"

static void stack_push_pop_success(void** state)
{
    (void)state;

    stack_t stack;

    stack_init(&stack, sizeof(int), 1);

    for (int i = 0; i < 10; i++)
    {
      assert_int_equal(0, stack_push(&stack, &i));
      assert_int_equal(i + 1, stack_get_count(&stack));
      assert_int_equal(i + 1, stack_get_size(&stack));
    }

    for (int i = 9; i >= 0; i--)
    {
      int popped_value;
      assert_int_equal(0, stack_pop(&stack, &popped_value));
      assert_int_equal(i, popped_value);
      assert_int_equal(i, stack_get_count(&stack));
      assert_int_equal(10, stack_get_size(&stack));
    }

    stack_deinit(&stack);
}

static void stack_top_returns_last_pushed_without_removing(void** state)
{
    (void)state;

    stack_t stack;
    stack_init(&stack, sizeof(int), 4);

    for (int i = 0; i < 3; i++)
    {
        assert_int_equal(0, stack_push(&stack, &i));
    }

    int top = -1;
    assert_int_equal(0, stack_top(&stack, &top));
    assert_int_equal(2, top);
    assert_int_equal(3, stack_get_count(&stack));

    /* top again must return the same value */
    top = -1;
    assert_int_equal(0, stack_top(&stack, &top));
    assert_int_equal(2, top);
    assert_int_equal(3, stack_get_count(&stack));

    stack_deinit(&stack);
}

static void stack_pop_on_empty_fails(void** state)
{
    (void)state;

    stack_t stack;
    stack_init(&stack, sizeof(int), 4);

    int v;
    assert_int_not_equal(0, stack_pop(&stack, &v));
    assert_int_equal(0, stack_get_count(&stack));

    stack_deinit(&stack);
}

static void stack_top_on_empty_fails(void** state)
{
    (void)state;

    stack_t stack;
    stack_init(&stack, sizeof(int), 4);

    int v;
    assert_int_not_equal(0, stack_top(&stack, &v));

    stack_deinit(&stack);
}

static void stack_grows_capacity_when_pushing_past_initial_size(void** state)
{
    (void)state;

    stack_t stack;
    stack_init(&stack, sizeof(int), 2);

    assert_int_equal(2, stack_get_size(&stack));

    for (int i = 0; i < 32; i++)
    {
        assert_int_equal(0, stack_push(&stack, &i));
    }

    assert_int_equal(32, stack_get_count(&stack));
    assert_true(stack_get_size(&stack) >= 32);

    stack_deinit(&stack);
}

static void stack_init_zero_initial_size_still_pushes(void** state)
{
    (void)state;

    stack_t stack;
    stack_init(&stack, sizeof(int), 0);

    int v = 42;
    assert_int_equal(0, stack_push(&stack, &v));

    int top = 0;
    assert_int_equal(0, stack_top(&stack, &top));
    assert_int_equal(42, top);

    stack_deinit(&stack);
}

int test_stack()
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(stack_push_pop_success),
      cmocka_unit_test(stack_top_returns_last_pushed_without_removing),
      cmocka_unit_test(stack_pop_on_empty_fails),
      cmocka_unit_test(stack_top_on_empty_fails),
      cmocka_unit_test(stack_grows_capacity_when_pushing_past_initial_size),
      cmocka_unit_test(stack_init_zero_initial_size_still_pushes),
  };

  return cmocka_run_group_tests_name("stack_tests", tests, NULL, NULL);
}
