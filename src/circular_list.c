#include "circular_list.h"


typedef struct LIST_NODE_STRUCT
{
    void* value;
    struct LIST_NODE_STRUCT* next;
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

void circular_list_destroy(CIRCULAR_LIST_HANDLE list);

LIST_NODE_HANDLE circular_list_add(CIRCULAR_LIST_HANDLE list, void* value);

int circular_list_remove(CIRCULAR_LIST_HANDLE list, LIST_NODE_HANDLE node);

LIST_NODE_HANDLE circular_list_get_head(CIRCULAR_LIST_HANDLE list)


LIST_NODE_HANDLE circular_list_node_get_next(LIST_NODE_HANDLE node);

LIST_NODE_HANDLE circular_list_node_get_previous(LIST_NODE_HANDLE node);

void* circular_list_node_get_value(LIST_NODE_HANDLE node);


#endif // CIRCULAR_LIST_H
