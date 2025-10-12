#include "list_misc.h"

static int remove_all(const void *const arg, const void *const value)
{
    if (arg || !value)
        return 0;

    return 1;
}

list_filter_t list_misc_init_remove_all(void)
{
    list_filter_t out = {
        .check = remove_all,
        .arg = NULL,
    };
    return out;
}


