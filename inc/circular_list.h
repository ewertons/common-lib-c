#ifndef CIRCULAR_LIST_H
#define CIRCULAR_LIST_H

#include <stdlib.h>
#include <stdbool.h>

typedef struct CIRCULAR_LIST_STRUCT* CIRCULAR_LIST_HANDLE;
typedef struct LIST_NODE_STRUCT* LIST_NODE_HANDLE;

typedef void(*LIST_ACTION)(LIST_NODE_HANDLE node, void* context, bool *continue_processing);
typedef bool (*REMOVE_CONDITION)(LIST_NODE_HANDLE node, void* context, bool *continue_processing);

extern CIRCULAR_LIST_HANDLE circular_list_create();
extern void circular_list_destroy(CIRCULAR_LIST_HANDLE list);
extern LIST_NODE_HANDLE circular_list_add(CIRCULAR_LIST_HANDLE list, void* value);
extern int circular_list_remove(CIRCULAR_LIST_HANDLE list, LIST_NODE_HANDLE node);
extern LIST_NODE_HANDLE circular_list_get_head(CIRCULAR_LIST_HANDLE list);
extern LIST_NODE_HANDLE circular_list_node_get_next(LIST_NODE_HANDLE node);
extern LIST_NODE_HANDLE circular_list_node_get_previous(LIST_NODE_HANDLE node);
extern void* circular_list_node_get_value(LIST_NODE_HANDLE node);
extern int circular_list_foreach(CIRCULAR_LIST_HANDLE list, LIST_ACTION action, void* context);
extern int circular_list_remove_if(CIRCULAR_LIST_HANDLE list, REMOVE_CONDITION condition, void* context);
extern int circular_list_to_array(CIRCULAR_LIST_HANDLE list, void*** array, int* length);

#endif // CIRCULAR_LIST_H
