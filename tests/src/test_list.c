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

static void list_init_null_safe(void** state)
{
    (void)state;
    list_init(NULL, sizeof(int));
    list_deinit(NULL);
}

static void list_is_empty_initially_true(void** state)
{
    (void)state;

    list_t list;
    list_init(&list, sizeof(int));

    assert_true(list_is_empty(&list));
    assert_true(list_is_empty(NULL));

    list_deinit(&list);
}

static void list_is_empty_after_add_false(void** state)
{
    (void)state;

    list_t list;
    int v = 1;
    list_init(&list, sizeof(int));

    assert_non_null(list_add(&list, &v));
    assert_false(list_is_empty(&list));

    list_deinit(&list);
}

static void list_get_head_returns_first_added(void** state)
{
    (void)state;

    list_t list;
    int v = 7;
    list_init(&list, sizeof(int));

    list_node_t* added = list_add(&list, &v);
    assert_non_null(added);

    list_node_t* head = list_get_head(&list);
    assert_ptr_equal(added, head);

    list_deinit(&list);
}

static void list_node_get_next_traverses(void** state)
{
    (void)state;

    list_t list;
    list_init(&list, sizeof(int));

    for (int i = 0; i < 4; i++)
    {
        assert_non_null(list_add(&list, &i));
    }

    list_node_t* n = list_get_head(&list);
    int v;
    for (int i = 0; i < 4; i++)
    {
        assert_non_null(n);
        assert_int_equal(0, list_node_get_data(&list, n, &v));
        assert_int_equal(i, v);
        n = list_node_get_next(n);
    }

    list_deinit(&list);
}

static void list_remove_head_success(void** state)
{
    (void)state;

    list_t list;
    list_init(&list, sizeof(int));

    int values[] = { 10, 20, 30 };
    list_node_t* nodes[3];
    for (int i = 0; i < 3; i++)
    {
        nodes[i] = list_add(&list, &values[i]);
    }

    assert_int_equal(0, list_remove(&list, nodes[0]));

    int v;
    list_node_t* head = list_get_head(&list);
    assert_int_equal(0, list_node_get_data(&list, head, &v));
    assert_int_equal(20, v);

    list_deinit(&list);
}

static void list_remove_only_node_makes_list_empty(void** state)
{
    (void)state;

    list_t list;
    list_init(&list, sizeof(int));

    int v = 99;
    list_node_t* n = list_add(&list, &v);
    assert_non_null(n);

    assert_int_equal(0, list_remove(&list, n));
    assert_true(list_is_empty(&list));

    list_deinit(&list);
}

static void list_remove_with_null_args_fails(void** state)
{
    (void)state;

    list_t list;
    list_init(&list, sizeof(int));
    int v = 1;
    list_node_t* n = list_add(&list, &v);

    assert_int_not_equal(0, list_remove(NULL, n));
    assert_int_not_equal(0, list_remove(&list, NULL));

    list_deinit(&list);
}

static bool sum_callback(list_node_t* node, void* context)
{
    int* sum = (int*)context;
    *sum += *(int*)node->data;
    return true;
}

static bool early_stop_callback(list_node_t* node, void* context)
{
    int* count = (int*)context;
    (void)node;
    (*count)++;
    return *count < 2;
}

static void list_foreach_visits_all_nodes(void** state)
{
    (void)state;

    list_t list;
    list_init(&list, sizeof(int));

    int values[] = { 1, 2, 3, 4, 5 };
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++)
    {
        assert_non_null(list_add(&list, &values[i]));
    }

    int sum = 0;
    assert_int_equal(0, list_foreach(&list, sum_callback, &sum));
    assert_int_equal(15, sum);

    list_deinit(&list);
}

static void list_foreach_early_stop(void** state)
{
    (void)state;

    list_t list;
    list_init(&list, sizeof(int));

    for (int i = 0; i < 5; i++)
    {
        assert_non_null(list_add(&list, &i));
    }

    int count = 0;
    assert_int_equal(0, list_foreach(&list, early_stop_callback, &count));
    assert_int_equal(2, count);

    list_deinit(&list);
}

static void list_foreach_invalid_args_fails(void** state)
{
    (void)state;

    list_t list;
    list_init(&list, sizeof(int));

    assert_int_not_equal(0, list_foreach(NULL, sum_callback, NULL));
    assert_int_not_equal(0, list_foreach(&list, NULL, NULL));

    list_deinit(&list);
}

static void list_foreach_on_empty_returns_ok(void** state)
{
    (void)state;

    list_t list;
    list_init(&list, sizeof(int));
    int sum = 0;

    assert_int_equal(0, list_foreach(&list, sum_callback, &sum));
    assert_int_equal(0, sum);

    list_deinit(&list);
}

static bool remove_even_callback(list_node_t* node, void* context, bool* remove)
{
    (void)context;
    int v = *(int*)node->data;
    *remove = (v % 2 == 0);
    return true;
}

static bool count_only_callback(list_node_t* node, void* context, bool* remove)
{
    int* count = (int*)context;
    (void)node;
    (*count)++;
    *remove = false;
    return true;
}

static void list_remove_if_removes_matching(void** state)
{
    (void)state;

    list_t list;
    list_init(&list, sizeof(int));

    for (int i = 0; i < 6; i++)
    {
        assert_non_null(list_add(&list, &i));
    }

    assert_int_equal(0, list_remove_if(&list, remove_even_callback, NULL));

    int sum = 0;
    assert_int_equal(0, list_foreach(&list, sum_callback, &sum));
    assert_int_equal(1 + 3 + 5, sum);

    list_deinit(&list);
}

static void list_remove_if_visits_all_when_no_removal(void** state)
{
    (void)state;

    list_t list;
    list_init(&list, sizeof(int));

    for (int i = 0; i < 4; i++)
    {
        assert_non_null(list_add(&list, &i));
    }

    int count = 0;
    assert_int_equal(0, list_remove_if(&list, count_only_callback, &count));
    assert_int_equal(4, count);

    list_deinit(&list);
}

static void list_remove_if_invalid_args_fails(void** state)
{
    (void)state;

    list_t list;
    list_init(&list, sizeof(int));

    assert_int_not_equal(0, list_remove_if(NULL, remove_even_callback, NULL));
    assert_int_not_equal(0, list_remove_if(&list, NULL, NULL));

    list_deinit(&list);
}

static void list_remove_if_on_empty_returns_ok(void** state)
{
    (void)state;

    list_t list;
    list_init(&list, sizeof(int));

    assert_int_equal(0, list_remove_if(&list, remove_even_callback, NULL));

    list_deinit(&list);
}

static void list_node_get_data_invalid_args_fails(void** state)
{
    (void)state;

    list_t list;
    list_init(&list, sizeof(int));
    int v = 7;
    list_node_t* n = list_add(&list, &v);
    int out;

    assert_int_not_equal(0, list_node_get_data(NULL, n, &out));
    assert_int_not_equal(0, list_node_get_data(&list, NULL, &out));
    assert_int_not_equal(0, list_node_get_data(&list, n, NULL));

    list_deinit(&list);
}

int test_list()
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(list_add_success),
        cmocka_unit_test(list_init_null_safe),
        cmocka_unit_test(list_is_empty_initially_true),
        cmocka_unit_test(list_is_empty_after_add_false),
        cmocka_unit_test(list_get_head_returns_first_added),
        cmocka_unit_test(list_node_get_next_traverses),
        cmocka_unit_test(list_remove_head_success),
        cmocka_unit_test(list_remove_only_node_makes_list_empty),
        cmocka_unit_test(list_remove_with_null_args_fails),
        cmocka_unit_test(list_foreach_visits_all_nodes),
        cmocka_unit_test(list_foreach_early_stop),
        cmocka_unit_test(list_foreach_invalid_args_fails),
        cmocka_unit_test(list_foreach_on_empty_returns_ok),
        cmocka_unit_test(list_remove_if_removes_matching),
        cmocka_unit_test(list_remove_if_visits_all_when_no_removal),
        cmocka_unit_test(list_remove_if_invalid_args_fails),
        cmocka_unit_test(list_remove_if_on_empty_returns_ok),
        cmocka_unit_test(list_node_get_data_invalid_args_fails),
    };

    return cmocka_run_group_tests_name("list_tests", tests, NULL, NULL);
}
