#include "list_misc.h"

static int remove_all(const void *const arg, const void *const value)
{
    if (arg || !value)
        return 0;

    return 1;
}

void list_misc_init_remove_all(list_filter_t *filter)
{
    if (NULL == filter)
        return;

    filter->check = remove_all;
    filter->arg = NULL;
}


