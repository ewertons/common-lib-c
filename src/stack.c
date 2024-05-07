#include "stack.h"
#include "niceties.h"
#include "string.h"

void stack_init(stack_t* stack, size_t value_size_in_bytes, int initial_size)
{
    if (stack != NULL && value_size_in_bytes > 0 && initial_size > 0)
    {
        stack->values = calloc(initial_size, value_size_in_bytes);
        stack->value_size = value_size_in_bytes;
        stack->size = initial_size;
        stack->count = 0;
    }
}

int stack_push(stack_t* stack, void* value)
{
    if (stack == NULL)
    {
        return ERROR;
    }
    else
    {
        if (stack->count >= stack->size)
        {
            uint8_t* new_values = realloc(stack->values, stack->value_size * (stack->size + 1));

            if (new_values == NULL)
            {
                return ERROR;
            }
            else
            {
                stack->values = new_values;
                stack->size++;
            }
        }

        (void)memcpy((stack->values + (stack->value_size * stack->count)), value, stack->value_size);
        stack->count++;

        return OK;
    }
}

int stack_pop(stack_t* stack, void* value)
{
    if (stack == NULL || stack->count == 0)
    {
        return ERROR;
    }
    else
    {
        if (value != NULL)
        {
            (void)memcpy(value, (stack->values + (stack->value_size * (stack->count - 1))), stack->value_size);
        }
        stack->count--;
        return OK;
    }
}

int stack_top(stack_t* stack, void* value)
{
    if (stack == NULL || stack->count == 0 || value == NULL)
    {
        return ERROR;
    }
    else
    {
        (void)memcpy(value, (stack->values + (stack->value_size * (stack->count - 1))), stack->value_size);
        return OK;
    }
}

int stack_get_count(stack_t* stack)
{
    if (stack == NULL)
    {
        return -1;
    }
    else
    {
        return stack->count;
    }
}

int stack_get_size(stack_t* stack)
{
    if (stack == NULL)
    {
        return -1;
    }
    else
    {
        return stack->size;
    }
}


