#include "list.h"
#include "niceties.h"
#include <string.h>

void list_init(list_t* list, size_t data_size)
{
    if (list != NULL && data_size > 0)
    {
        list->root = NULL;
        list->data_size = data_size;
    }
}

void list_deinit(list_t* list)
{
    if (list != NULL && list->root != NULL)
    {
        list_node_t* current = list->root;
        list_node_t* next;

        list->root->previous->next = NULL;

        while (current != NULL)
        {
            next = current->next;
            free(current->data);
            free(current);
            current = next;
        }
    }
}

list_node_t* list_add(list_t* list, void* data)
{
    list_node_t* new_node = NULL;

    if (list != NULL)
    {
        new_node = malloc(sizeof(list_node_t));

        if (new_node != NULL)
        {
            new_node->data = malloc(list->data_size);

            if (new_node->data == NULL)
            {
                free(new_node);
                new_node = NULL;
            }
            else
            {
                (void)memcpy(new_node->data, data, list->data_size);

                if (list->root == NULL)
                {
                    list->root = new_node;
                    new_node->next = new_node;
                    new_node->previous = new_node;
                }
                else
                {
                    new_node->previous = list->root->previous;
                    new_node->next = list->root;
                    list->root->previous->next = new_node;
                    list->root->previous = new_node;
                }
            }
        }
    }

    return new_node;
}

static void remove_node(list_t* list, list_node_t* node)
{
    if (node->next == node)
    {
        list->root = NULL;
    }
    else
    {
        if (list->root == node)
        {
            list->root = node->next;
        }

        node->previous->next = node->next;
        node->next->previous = node->previous;
    }

    free(node->data);
    free(node);
}

int list_remove(list_t* list, list_node_t* node)
{
    if (list == NULL || node == NULL)
    {
        return ERROR;
    }
    else
    {
        remove_node(list, node);

        return OK;
    }
}

int list_foreach(list_t* list, list_foreach_callback_t callback, void* context)
{
    if (list == NULL || callback == NULL)
    {
        return ERROR;
    }
    else if (list->root == NULL)
    {
        return OK;
    }
    else
    {
        list_node_t* current = list->root;

        do
        {
            if (!callback(current, context))
            {
                break;
            }

            current = current->next;
        }
        while (current != list->root);

        return OK;
    }
}

int list_remove_if(list_t* list, list_remove_callback_t callback, void* context)
{
    if (list == NULL || callback == NULL)
    {
        return ERROR;
    }
    else if (list->root == NULL)
    {
        return OK;
    }
    else
    {
        list_node_t* current = list->root;
        list_node_t* next = NULL;
        bool continue_processing = true;

        do
        {
            bool remove = false;
            continue_processing = callback(current, context, &remove);

            if (current->next == list->root)
            {
                continue_processing = false;
            }
            else
            {
                next = current->next;
            }

            if (remove)
            {
                remove_node(list, current);
            }

            current = next;
        }
        while (continue_processing);

        return OK;
    }
}

int list_node_get_data(list_t* list, list_node_t* node, void* out_data)
{
    if (list == NULL || node == NULL || out_data == NULL)
    {
        return ERROR;
    }
    else
    {
        (void)memcpy(out_data, node->data, list->data_size);

        return OK;
    }
}

bool list_is_empty(list_t* list)
{
    return (list == NULL || list->root == NULL);
}

list_node_t* list_get_head(list_t* list)
{
    return list != NULL ? list->root : NULL;
}

list_node_t* list_node_get_next(list_node_t* node)
{
    return node != NULL ? node->next : NULL;
}
