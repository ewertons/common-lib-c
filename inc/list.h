#ifndef LIST_H
#define LIST_H

#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>

typedef struct list_node
{
    uint8_t* data;
    struct list_node* previous;
    struct list_node* next;
} list_node_t;

typedef struct list
{
    struct list_node* root;
    size_t data_size;
} list_t;

typedef bool (*list_foreach_callback_t)(list_node_t* node, void* context);
typedef bool (*list_remove_callback_t)(list_node_t* node, void* context, bool* remove);

void list_init(list_t* list, size_t data_size);
void list_deinit(list_t* list);
list_node_t* list_get_head(list_t* list);
list_node_t* list_add(list_t* list, void* data);
int list_remove(list_t* list, list_node_t* node);
int list_foreach(list_t* list, list_foreach_callback_t callback, void* context);
int list_remove_if(list_t* list, list_remove_callback_t callback, void* context);
int list_node_get_data(list_t* list, list_node_t* node, void* out_data);
list_node_t* list_node_get_next(list_node_t* node);
bool list_is_empty(list_t* list);

#endif // LIST_H
