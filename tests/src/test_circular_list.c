#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

#include "circular_list.h"
#include "test_circular_list.h"

static void circular_list_create_success(void** state)
{
    (void)state;

    int result;
    CIRCULAR_LIST_HANDLE list = circular_list_create();

    assert_non_null(list);

    circular_list_destroy(list);
}

static bool remove_item(LIST_NODE_HANDLE node, void* context, bool* continue_processing)
{
    int* value = (int*)circular_list_node_get_value(node);
    int* ref = (int*)context;

    *continue_processing = true;

    return *value == *ref;
}

static void foreach_integer_sum(LIST_NODE_HANDLE node, void* context, bool* continue_processing)
{
    int* value = (int*)circular_list_node_get_value(node);
    int* sum = (int*)context;
    
    *sum += *value;

    *continue_processing = true;
}

static void circular_list_foreach_success(void** state)
{
    (void)state;

    int result;
    int a = 10, b = 11, c = 12;
    int sum = 0;

    CIRCULAR_LIST_HANDLE list = circular_list_create();

    assert_non_null(list);
    assert_non_null(circular_list_add(list, &a));
    assert_non_null(circular_list_add(list, &b));
    assert_non_null(circular_list_add(list, &c));

    result = circular_list_foreach(list, foreach_integer_sum, &sum);

    assert_int_equal(result, 0);
    assert_int_equal(sum, a + b + c);

    circular_list_destroy(list);
}

static void circular_list_remove_if_success(void** state)
{
    (void)state;

    int result;
    int a = 10, b = 11, c = 12;
    int sum = 0;
    CIRCULAR_LIST_HANDLE list = circular_list_create();
    
    assert_non_null(list);
    assert_non_null(circular_list_add(list, &a));
    assert_non_null(circular_list_add(list, &b));
    assert_non_null(circular_list_add(list, &c));

    assert_int_equal(circular_list_remove_if(list, remove_item, &b), 0);
    assert_int_equal(circular_list_foreach(list, foreach_integer_sum, &sum), 0);
    assert_int_equal(sum, a + c);

    circular_list_destroy(list);
}

static void circular_list_to_array_success(void** state)
{
    (void)state;

    int result;
    int a = 10, b = 11, c = 12;
    CIRCULAR_LIST_HANDLE list = circular_list_create();
    void** array;
    int array_length;

    circular_list_add(list, &a);
    circular_list_add(list, &b);
    circular_list_add(list, &c);

    assert_int_equal(circular_list_to_array(list, &array, &array_length), 0);
    assert_int_equal(*(int*)array[0], a);
    assert_int_equal(*(int*)array[1], b);
    assert_int_equal(*(int*)array[2], c);

    free(array);
    circular_list_destroy(list);
}

int test_circular_list()
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(circular_list_create_success),
      cmocka_unit_test(circular_list_foreach_success),
      cmocka_unit_test(circular_list_remove_if_success),
      cmocka_unit_test(circular_list_to_array_success)
  };

  return cmocka_run_group_tests_name("circular_list_tests", tests, NULL, NULL);
}
