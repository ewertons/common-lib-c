#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

#include "tests.h"
#include "list.h"

static void list_add_success(void** state)
{
  (void)state;

  list_t list;
  int value = 10;
  int out_value = -1;
  list_node_t* node;

  list_init(&list, sizeof(int));
  node = list_add(&list, &value);
  assert_non_null(node);
  assert_int_equal(0, list_node_get_data(&list, node, &out_value));
  assert_int_equal(value, out_value);

  list_deinit(&list);
}

int test_list()
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(list_add_success),
  };

  return cmocka_run_group_tests_name("list_tests", tests, NULL, NULL);
}
