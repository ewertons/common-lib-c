#ifndef BST_REDBLACK
#define BST_REDBLACK

#include <stdlib.h>
#include <stdbool.h>

typedef struct bst_rb_node
{
    int value;
    struct bst_rb_node* parent;
    struct bst_rb_node* left;
    struct bst_rb_node* right;
    bool is_red;
} bst_rb_node_t;

typedef enum bst_search_order
{
    bst_search_order_dfs_pre_order,
    bst_search_order_dfs_in_order,
    bst_search_order_dfs_post_order,
    bst_search_order_bfs
} bst_search_order_t;

typedef void (*bst_rb_traverse_callback_t)(bst_rb_node_t* node, void* context);

bst_rb_node_t* bst_rb_add(bst_rb_node_t* root, int value);

int bst_rb_remove(bst_rb_node_t* root, int value);

int bst_rb_node_get_value(bst_rb_node_t* node, int* value);

void bst_rb_traverse(bst_rb_node_t* root, bst_search_order_t order, bst_rb_traverse_callback_t callback, void* context);

#endif // BST_REDBLACK