#ifndef STACK_H
#define STACK_H

#include <stdlib.h>
#include <inttypes.h>

typedef struct stack
{
    uint8_t* values;
    size_t value_size;
    int size;
    int count;
} clc_stack_t;

void stack_init(clc_stack_t* stack, size_t value_size_in_bytes, int initial_size);
void stack_deinit(clc_stack_t* stack);
int stack_push(clc_stack_t* stack, void* value);
int stack_pop(clc_stack_t* stack, void* value);
int stack_top(clc_stack_t* stack, void* value);
int stack_get_count(clc_stack_t* stack);
int stack_get_size(clc_stack_t* stack);

#endif // STACK_H
