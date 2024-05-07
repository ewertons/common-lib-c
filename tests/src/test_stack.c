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
}

int test_stack()
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(stack_push_pop_success),
  };

  return cmocka_run_group_tests_name("stack_tests", tests, NULL, NULL);
}
