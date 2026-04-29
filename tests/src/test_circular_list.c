#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

#include "circular_list.h"
#include "tests.h"

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

static void circular_list_get_head_traverses(void** state)
{
    (void)state;

    int a = 1, b = 2, c = 3;
    CIRCULAR_LIST_HANDLE list = circular_list_create();

    LIST_NODE_HANDLE n1 = circular_list_add(list, &a);
    circular_list_add(list, &b);
    circular_list_add(list, &c);

    LIST_NODE_HANDLE head = circular_list_get_head(list);
    assert_ptr_equal(n1, head);

    /* Traverse forward via node_get_next and confirm circular chain. */
    LIST_NODE_HANDLE n = head;
    int seen[3];
    for (int i = 0; i < 3; i++)
    {
        assert_non_null(n);
        seen[i] = *(int*)circular_list_node_get_value(n);
        n = circular_list_node_get_next(n);
    }
    assert_int_equal(a, seen[0]);
    assert_int_equal(b, seen[1]);
    assert_int_equal(c, seen[2]);

    circular_list_destroy(list);
}

static void circular_list_remove_specific_node(void** state)
{
    (void)state;

    int a = 1, b = 2, c = 3;
    CIRCULAR_LIST_HANDLE list = circular_list_create();

    circular_list_add(list, &a);
    LIST_NODE_HANDLE nb = circular_list_add(list, &b);
    circular_list_add(list, &c);

    assert_int_equal(0, circular_list_remove(list, nb));

    int sum = 0;
    assert_int_equal(0, circular_list_foreach(list, foreach_integer_sum, &sum));
    assert_int_equal(a + c, sum);

    circular_list_destroy(list);
}

static void circular_list_node_get_value_returns_payload(void** state)
{
    (void)state;

    int v = 42;
    CIRCULAR_LIST_HANDLE list = circular_list_create();
    LIST_NODE_HANDLE n = circular_list_add(list, &v);

    int* p = (int*)circular_list_node_get_value(n);
    assert_non_null(p);
    assert_int_equal(42, *p);

    circular_list_destroy(list);
}

static void circular_list_node_get_previous_traverses_back(void** state)
{
    (void)state;

    int a = 1, b = 2, c = 3;
    CIRCULAR_LIST_HANDLE list = circular_list_create();

    LIST_NODE_HANDLE n1 = circular_list_add(list, &a);
    LIST_NODE_HANDLE n2 = circular_list_add(list, &b);
    LIST_NODE_HANDLE n3 = circular_list_add(list, &c);

    /* previous(n3) should be n2, previous(n2) should be n1, previous(n1) should
     * wrap around to n3. */
    assert_ptr_equal(n2, circular_list_node_get_previous(n3));
    assert_ptr_equal(n1, circular_list_node_get_previous(n2));
    assert_ptr_equal(n3, circular_list_node_get_previous(n1));

    circular_list_destroy(list);
}

static void early_stop_action(LIST_NODE_HANDLE node, void* context, bool* continue_processing)
{
    int* count = (int*)context;
    (void)node;
    (*count)++;
    *continue_processing = (*count < 2);
}

static void circular_list_foreach_early_stop(void** state)
{
    (void)state;

    int a = 1, b = 2, c = 3;
    CIRCULAR_LIST_HANDLE list = circular_list_create();
    circular_list_add(list, &a);
    circular_list_add(list, &b);
    circular_list_add(list, &c);

    int count = 0;
    assert_int_equal(0, circular_list_foreach(list, early_stop_action, &count));
    assert_int_equal(2, count);

    circular_list_destroy(list);
}

static void circular_list_to_array_on_empty_list(void** state)
{
    (void)state;

    CIRCULAR_LIST_HANDLE list = circular_list_create();
    void** array = NULL;
    int length = -1;

    assert_int_equal(0, circular_list_to_array(list, &array, &length));
    assert_int_equal(0, length);

    if (array != NULL)
    {
        free(array);
    }

    circular_list_destroy(list);
}

int test_circular_list()
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(circular_list_create_success),
      cmocka_unit_test(circular_list_foreach_success),
      cmocka_unit_test(circular_list_remove_if_success),
      cmocka_unit_test(circular_list_to_array_success),
      cmocka_unit_test(circular_list_get_head_traverses),
      cmocka_unit_test(circular_list_remove_specific_node),
      cmocka_unit_test(circular_list_node_get_value_returns_payload),
      cmocka_unit_test(circular_list_node_get_previous_traverses_back),
      cmocka_unit_test(circular_list_foreach_early_stop),
      cmocka_unit_test(circular_list_to_array_on_empty_list),
  };

  return cmocka_run_group_tests_name("circular_list_tests", tests, NULL, NULL);
}
