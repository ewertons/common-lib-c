#ifndef CIRCULAR_LIST_H
#define CIRCULAR_LIST_H

#include <stdlib.h>

typedef struct CIRCULAR_LIST_STRUCT* CIRCULAR_LIST_HANDLE;
typedef struct LIST_NODE_STRUCT* LIST_NODE_HANDLE;

extern CIRCULAR_LIST_HANDLE circular_list_create();
extern void circular_list_destroy(CIRCULAR_LIST_HANDLE list);
extern LIST_NODE_HANDLE circular_list_add(CIRCULAR_LIST_HANDLE list, void* value);
extern int circular_list_remove(CIRCULAR_LIST_HANDLE list, LIST_NODE_HANDLE node);
extern LIST_NODE_HANDLE circular_list_get_head(CIRCULAR_LIST_HANDLE list);
extern LIST_NODE_HANDLE circular_list_node_get_next(LIST_NODE_HANDLE node);
extern LIST_NODE_HANDLE circular_list_node_get_previous(LIST_NODE_HANDLE node);
extern void* circular_list_node_get_value(LIST_NODE_HANDLE node);


#endif // CIRCULAR_LIST_H
