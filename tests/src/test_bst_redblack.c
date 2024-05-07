#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

#include "tests.h"
#include "bst_redblack.h"
#include "niceties.h"

typedef struct test_value_set
{
    int values[20];
    int count;
} test_value_set_t;

static void store_values_in_test_set(bst_rb_node_t* node, void* context)
{
    test_value_set_t* value_set = (test_value_set_t*)context;

    if (bst_rb_node_get_value(node, &(value_set->values[value_set->count])) != OK)
    {
        return;
    }

    value_set->count++;
}

static void print_tree_node(bst_rb_node_t* node, void* context)
{
    int value;

    if (bst_rb_node_get_value(node, &value) != OK)
    {
        return;
    }

    printf("%d ", value);
    fflush(NULL);
}

static void bst_rb_add_success(void** state)
{
    (void)state;

    bst_rb_node_t* root = bst_rb_add(NULL, 10);

    assert_non_null(root);
    assert_int_equal(root->value, 10);

    (void)bst_rb_traverse(root, bst_search_order_dfs_pre_order, print_tree_node, NULL);
    printf("\n");
}

static void bst_rb_add_multiple_success(void** state)
{
    (void)state;
    int values[] = { 10, 5, 111, 21, -1, 8, 88, 50, 31, -2, 71, 66, 13 };
    int values_preorder[] = { 10, 5, -1, -2, 8, 111, 21, 13, 88, 50, 31, 71, 66 };
    bst_rb_node_t* root = NULL;

    for (int i = 0; i < sizeofarray(values); i++)
    {
        if (root == NULL)
        {
            root = bst_rb_add(NULL, 10);
            assert_non_null(root);
        }
        else
        {
            assert_non_null(bst_rb_add(root, values[i]));
        }
    }

    test_value_set_t value_set = { 0 };
    (void)bst_rb_traverse(root, bst_search_order_dfs_pre_order, store_values_in_test_set, &value_set);
    assert_int_equal(value_set.count, sizeofarray(values_preorder));
    assert_memory_equal(value_set.values, values_preorder, sizeofarray(values_preorder));

    (void)bst_rb_traverse(root, bst_search_order_dfs_pre_order, print_tree_node, NULL);
    printf("\n");
}

static void bst_rb_traverse_in_order_success(void** state)
{
    (void)state;
    int values[] = { 10, 5, 111, 21, -1, 8, 88, 50, 31, -2, 71, 66, 13 };
    int values_in_order[] = { -2, -1, 5, 8, 10, 13, 21, 31, 50, 66, 71, 88, 111 };
    bst_rb_node_t* root = NULL;

    for (int i = 0; i < sizeofarray(values); i++)
    {
        if (root == NULL)
        {
            root = bst_rb_add(NULL, 10);
            assert_non_null(root);
        }
        else
        {
            assert_non_null(bst_rb_add(root, values[i]));
        }
    }

    (void)bst_rb_traverse(root, bst_search_order_dfs_in_order, print_tree_node, NULL);
    printf("\n");

    test_value_set_t value_set = { 0 };
    (void)bst_rb_traverse(root, bst_search_order_dfs_in_order, store_values_in_test_set, &value_set);
    assert_int_equal(value_set.count, sizeofarray(values_in_order));
    assert_memory_equal(value_set.values, values_in_order, sizeofarray(values_in_order));
}

static void bst_rb_traverse_post_order_success(void** state)
{
    (void)state;
    int values[] = { 10, 5, 111, 21, -1, 8, 88, 50, 31, -2, 71, 66, 13 };
    int values_post_order[] = { -2, -1, 8, 5, 13, 31, 66, 71, 50, 88, 21, 111, 10 };
    bst_rb_node_t* root = NULL;

    for (int i = 0; i < sizeofarray(values); i++)
    {
        if (root == NULL)
        {
            root = bst_rb_add(NULL, 10);
            assert_non_null(root);
        }
        else
        {
            assert_non_null(bst_rb_add(root, values[i]));
        }
    }

    (void)bst_rb_traverse(root, bst_search_order_dfs_post_order, print_tree_node, NULL);
    printf("\n");

    test_value_set_t value_set = { 0 };
    (void)bst_rb_traverse(root, bst_search_order_dfs_post_order, store_values_in_test_set, &value_set);
    assert_int_equal(value_set.count, sizeofarray(values_post_order));
    assert_memory_equal(value_set.values, values_post_order, sizeofarray(values_post_order));
}

int test_bst_redblack()
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(bst_rb_add_success),
      cmocka_unit_test(bst_rb_add_multiple_success),
      cmocka_unit_test(bst_rb_traverse_in_order_success),
      cmocka_unit_test(bst_rb_traverse_post_order_success),
  };

  return cmocka_run_group_tests_name("bst_redblack_tests", tests, NULL, NULL);
}
