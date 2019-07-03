#include "circular_list.h"

static int circular_list_create_test()
{
    int result;
    CIRCULAR_LIST_HANDLE list = circular_list_create();

    if (list != NULL)
    {
        result = 0;
        circular_list_destroy(list);
    }
    else
    {
        result = 1;
    }

    return result;
}

static bool remove_item(LIST_NODE_HANDLE node, void* context, bool* continue_processing)
{
    int* value = (int*)circular_list_node_get_value(node);
    int* ref = (int*)context;

    *continue_processing = true;

    return *value == *ref;
}

static void foreach_integer_add_X(LIST_NODE_HANDLE node, void* context, bool* continue_processing)
{
    int* value = (int*)circular_list_node_get_value(node);
    int* add = (int*)context;
    
    *value += *add;

    *continue_processing = true;
}

static int circular_list_foreach_test()
{
    int result;
    int a = 10, b = 11, c = 12;
    int add = 33;

    CIRCULAR_LIST_HANDLE list = circular_list_create();

    circular_list_add(list, &a);
    circular_list_add(list, &b);
    circular_list_add(list, &c);

    if (circular_list_foreach(list, foreach_integer_add_X, &add) != 0)
    {
        result = 1;
    }
    else if ((a + b + c) != 132)
    {
        result = 1;
    }
    else
    {
        result = 0;
    }

    circular_list_destroy(list);

    return result;
}

static int circular_list_remove_if_test()
{
    int result;
    int a = 10, b = 11, c = 12;
    CIRCULAR_LIST_HANDLE list = circular_list_create();
    
    circular_list_add(list, &a);
    circular_list_add(list, &b);
    circular_list_add(list, &c);

    if (circular_list_remove_if(list, remove_item, &b) != 0)
    {
        result = 1;
    }
    else
    {
        int add = 33;
        
        if (circular_list_foreach(list, foreach_integer_add_X, &add) != 0)
        {
            result = 1;
        }
        else if ((a + b + c) != 99)
        {
            result = 1;
        }
        else
        {
            result = 0;
        }
    }

    circular_list_destroy(list);

    return result;
}


static int circular_list_to_array_test()
{
    int result;
    int a = 10, b = 11, c = 12;
    CIRCULAR_LIST_HANDLE list = circular_list_create();
    void** array;
    int array_length;

    circular_list_add(list, &a);
    circular_list_add(list, &b);
    circular_list_add(list, &c);

    if (circular_list_to_array(list, &array, &array_length) != 0)
    {
        result = 1;
    }
    else
    {
        if (array_length != 3)
        {
            result = 1;
        }
        else if (*(int*)array[0] != a)
        {
            result = 1;
        }
        else if (*(int*)array[1] != b)
        {
            result = 1;
        }
        else if (*(int*)array[2] != c)
        {
            result = 1;
        }
        else
        {
            result = 0;
        }

        free(array);
    }

    circular_list_destroy(list);

    return result;
}

int main(void)
{
    int result = 0;

    result += circular_list_create_test();
    result += circular_list_foreach_test();
    result += circular_list_remove_if_test();
    result += circular_list_to_array_test();

    return result;
}