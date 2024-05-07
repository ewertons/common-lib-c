#include <string.h>

#include "bst_redblack.h"
#include "stack.h"
#include "niceties.h"

static bst_rb_node_t* node_create(int value)
{
    bst_rb_node_t* new_node = calloc(1, sizeof(bst_rb_node_t));
    new_node->value = value;
    return new_node;
}

bst_rb_node_t* bst_rb_add(bst_rb_node_t* root, int value)
{
    bst_rb_node_t* new_node = calloc(1, sizeof(bst_rb_node_t));

    if (new_node != NULL)
    {
        new_node->value = value;
        new_node->is_red = false;

        if (root != NULL)
        {
            bst_rb_node_t* current_node = root;

            while (true)
            {
                if (value <= current_node->value)
                {
                    if (current_node->left != NULL)
                    {
                        current_node = current_node->left;
                    }
                    else
                    {
                        new_node->parent = current_node;
                        current_node->left = new_node;
                        new_node->is_red = true;
                        break;
                    }
                }
                else // (value > current_node->value)
                {
                    if (current_node->right != NULL)
                    {
                        current_node = current_node->right;
                    }
                    else
                    {
                        new_node->parent = current_node;
                        current_node->right = new_node;
                        new_node->is_red = true;
                        break;
                    }
                }
            }
        }
    }

    return new_node;
}

int bst_rb_remove(bst_rb_node_t* root, int value)
{
    (void)root;
    (void)value;
    return 0; 
}

int bst_rb_node_get_value(bst_rb_node_t* node, int* value)
{
    if (node == NULL || value == NULL)
    {
        return ERROR;
    }
    else
    {
        (void)memcpy(value, &node->value, sizeof(int));

        return OK;
    }
}


void bst_rb_traverse(bst_rb_node_t* root, bst_search_order_t order, bst_rb_traverse_callback_t callback, void* context)
{
    if (root != NULL && callback != NULL)
    {
        if (order == bst_search_order_dfs_pre_order)
        {
            stack_t stack;
            bst_rb_node_t* node = root;

            stack_init(&stack, sizeof(bst_rb_node_t*), 1);

            while(true)
            {
                if (node != NULL)
                {
                    callback(node, context);

                    if (node->right != NULL)
                    {
                        if (stack_push(&stack, &(node->right)) != OK)
                        {
                            return;
                        }
                    }

                    node = node->left;
                }
                else if (stack_get_count(&stack) > 0)
                {
                    if (stack_pop(&stack, &node) != OK)
                    {
                        return;
                    }
                }
                else
                {
                    break;
                }
            }
        }
        else if (order == bst_search_order_dfs_in_order)
        {
            stack_t stack;
            bst_rb_node_t* node = root;

            stack_init(&stack, sizeof(bst_rb_node_t*), 1);

            while(true)
            {
                if (node != NULL)
                {
                    if (stack_push(&stack, &node) != OK)
                    {
                        return;
                    }

                    node = node->left;
                }
                else if (stack_get_count(&stack) > 0)
                {
                    if (stack_pop(&stack, &node) != OK)
                    {
                        return;
                    }

                    callback(node, context);

                    node = node->right;
                }
                else
                {
                    break;
                }
            }
        }
        else if (order == bst_search_order_dfs_post_order)
        {
            stack_t stack;
            bst_rb_node_t* previous_node = NULL;

            stack_init(&stack, sizeof(bst_rb_node_t*), 1);

            while(true)
            {
                if (root != NULL)
                {
                    if (stack_push(&stack, &root) != OK)
                    {
                        return;
                    }

                    root = root->left;
                }
                else if (stack_get_count(&stack) > 0)
                {
                    bst_rb_node_t* current_node;

                    if (stack_top(&stack, &current_node) != OK)
                    {
                        return;
                    }

                    if (current_node->right != NULL && current_node->right != previous_node)
                    {
                        root = current_node->right;
                    }
                    else
                    {
                        callback(current_node, context);
                        previous_node = current_node;

                        if (stack_pop(&stack, NULL) != OK)
                        {
                            return;
                        }
                    }
                }
                else
                {
                    break;
                }
            }
        }
        else // if (order == bst_search_order_bfs)
        {

        }
    }
}

