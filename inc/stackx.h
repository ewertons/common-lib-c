#ifndef STACKX_H
#define STACKX_H

#include <stdlib.h>
#include <inttypes.h>

typedef struct stackx
{
    uint8_t* values;
    size_t value_size;
    int size;
    int count;
} stackx_t;

void stackx_init(stackx_t* stack, size_t value_size_in_bytes, int initial_size);
void stackx_deinit(stackx_t* stack);
int stackx_push(stackx_t* stack, void* value);
int stackx_pop(stackx_t* stack, void* value);
int stackx_top(stackx_t* stack, void* value);
int stackx_get_count(stackx_t* stack);
int stackx_get_size(stackx_t* stack);

#endif // STACKX_H
