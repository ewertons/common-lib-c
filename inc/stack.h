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
} stack_t;

void stack_init(stack_t* stack, size_t value_size_in_bytes, int initial_size);
int stack_push(stack_t* stack, void* value);
int stack_pop(stack_t* stack, void* value);
int stack_top(stack_t* stack, void* value);
int stack_get_count(stack_t* stack);
int stack_get_size(stack_t* stack);

#endif // STACK_H
