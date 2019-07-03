#include "circular_list.h"


typedef struct LIST_NODE_STRUCT
{
    void* value;
    struct LIST_NODE_STRUCT* next;
    struct LIST_NODE_STRUCT* previous;
} LIST_NODE;

typedef struct CIRCULAR_LIST_STRUCT
{
    LIST_NODE* head;
} CIRCULAR_LIST;


CIRCULAR_LIST_HANDLE circular_list_create()
{
    CIRCULAR_LIST* result;

    if ((result = malloc(sizeof(CIRCULAR_LIST))) == NULL)
    {
        // error handling
    }
    else
    {
        result->head = NULL;
    }

    return result;
}

void circular_list_destroy(CIRCULAR_LIST_HANDLE list)
{
    if (list == NULL)
    {
    }
    else
    {
        if (list->head != NULL)
        {
            list->head->previous->next = NULL;

            while (list->head != NULL)
            {
                LIST_NODE* node = list->head;
                list->head = list->head->next;
                free(node);
            }
        }

        free(list);
    }
}

LIST_NODE_HANDLE circular_list_add(CIRCULAR_LIST_HANDLE list, void* value)
{
    LIST_NODE* result;

    if (list == NULL)
    {
        result = NULL;
    }
    else if ((result = malloc(sizeof(LIST_NODE))) == NULL)
    {
        
    }
    else
    {
        result->value = value;

        if (list->head == NULL)
        {
            list->head = result;
            result->next = result;
            result->previous = result;
        }
        else
        {
            result->previous = list->head->previous;
            result->next = list->head;
            list->head->previous->next = result;
            list->head->previous = result;
        }
    }

    return result;
}

int circular_list_remove(CIRCULAR_LIST_HANDLE list, LIST_NODE_HANDLE node)
{
    int result;

    if (list == NULL || list->head == NULL || node == NULL)
    {
        result = 1;
    }
    else
    {
        if (node->next == node->previous)
        {
            list->head = NULL;
        }
        else
        {
            if (list->head == node)
            {
                list->head = node->next;
            }

            node->previous->next = node->next;
            node->next->previous = node->previous;
        }
        
        free(node);

        result = 0;
    }
    
    return result;
}

LIST_NODE_HANDLE circular_list_get_head(CIRCULAR_LIST_HANDLE list)
{
    LIST_NODE* result;

    if (list == NULL)
    {
        // error handling;
        result = NULL;
    }
    else
    {
        result = list->head;
    }

    return result;
}

LIST_NODE_HANDLE circular_list_node_get_next(LIST_NODE_HANDLE node)
{
    LIST_NODE* result;

    if (node == NULL)
    {
        // error handling;
        result = NULL;
    }
    else
    {
        result = node->next;
    }

    return result;
}

LIST_NODE_HANDLE circular_list_node_get_previous(LIST_NODE_HANDLE node)
{
    LIST_NODE* result;

    if (node == NULL)
    {
        // error handling;
        result = NULL;
    }
    else
    {
        result = node->previous;
    }

    return result;
}

void* circular_list_node_get_value(LIST_NODE_HANDLE node)
{
    void* result;

    if (node == NULL)
    {
        // error handling;
        result = NULL;
    }
    else
    {
        result = node->value;
    }

    return result;
}

int circular_list_foreach(CIRCULAR_LIST_HANDLE list, LIST_ACTION action, void* context)
{
    int result;

    if (list == NULL || action == NULL)
    {
        result = __LINE__;
    }
    else if (list->head == NULL)
    {
        result = 0;
    }
    else
    {
        LIST_NODE* node = list->head;
        bool continue_processing = true;

        do
        {
            action(node, context, &continue_processing);

            node = node->next;
        } 
        while (node != list->head && continue_processing);

        result = 0;
    }

    return result;
}

int circular_list_remove_if(CIRCULAR_LIST_HANDLE list, REMOVE_CONDITION condition, void* context)
{
    int result;

    if (list == NULL || condition == NULL)
    {
        result = __LINE__;
    }
    else if (list->head == NULL)
    {
        result = 0;
    }
    else
    {
        LIST_NODE* node = list->head;
        bool continue_processing = true;

        do
        {
            if (condition(node, context, &continue_processing))
            {
                if (node->next == node)
                {
                    free(node);
                    node = NULL;
                    list->head = NULL;
                }
                else
                {
                    LIST_NODE* temp_node = node;

                    node->next->previous = node->previous;
                    node->previous->next = node->next;
                    
                    if (list->head == node)
                    {
                        list->head = node->next;
                    }

                    node = node->next;

                    free(temp_node);
                }
            }
            else
            {
                node = node->next;
            }
        } while (node != list->head && continue_processing);

        result = 0;
    }

    return result;
}

int circular_list_to_array(CIRCULAR_LIST_HANDLE list, void*** array, int* length)
{
    int result;

    if (list == NULL || array == NULL || length == NULL)
    {
        result = __LINE__;
    }
    else
    {
        LIST_NODE* node = list->head;

        if (node == NULL)
        {
            *length = 0;
            result = 0;
        }
        else
        {
            int temp_length = 0;
            void** temp_array;

            do
            {
                temp_length++;
                node = node->next;
            } while (node != list->head);

            temp_array = malloc(sizeof(void*) * temp_length);

            if (temp_array == NULL)
            {
                result = __LINE__;
            }
            else
            {
                int i = 0;

                do
                {
                    temp_array[i] = node->value;
                    node = node->next;
                    i++;
                } while (node != list->head);

                *array = temp_array;
                *length = temp_length;

                result = 0;
            }
        }
    }

    return result;
}