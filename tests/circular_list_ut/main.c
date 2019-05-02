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

int main(void)
{
    int result = 0;

    result += circular_list_create_test();

    return result;
}