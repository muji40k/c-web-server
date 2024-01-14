#include "handler.h"
#include "list.h"

struct _handler_list
{
    list_t *list;
};

struct _handler_call
{
    handler_func_t function;
    void *arg;
};

static int handler_check(const handler_t *const handler);
static int handler_find(const void *const arg, const void *const value);

handler_call_t *handler_call_init(void)
{
    handler_call_t *out = malloc(sizeof(handler_call_t));

    if (NULL == out)
        return errno = ERROR_HANDLER_LIST_ALLOCATION, NULL;

    out->function = NULL;
    out->arg = NULL;

    return out;
}

int handler_call(const handler_call_t *const call, const int fd,
                 const request_t *const request)
{
    if (NULL == call)
        return ERROR_HANDLER_LIST_NULL;

    if (NULL == call->function)
        return ERROR_HANDLER_LIST_INVALID_CALL;

    return call->function(fd, request, call->arg);
}

void handler_call_free(handler_call_t **const call)
{
    if (NULL == call || NULL == *call)
        return;

    free(*call);
    *call = NULL;
}

handler_list_t *handler_list_init(void)
{
    handler_list_t *out = malloc(sizeof(handler_list_t));

    if (NULL == out)
        return errno = ERROR_HANDLER_LIST_ALLOCATION, NULL;

    out->list = list_init(sizeof(handler_t));

    if (NULL == out)
        return errno = ERROR_HANDLER_LIST_ALLOCATION, NULL;

    return out;
}

int handler_list_push(handler_list_t *list, const handler_t *const handler)
{
    if (NULL == list)
        return ERROR_HANDLER_LIST_NULL;

    int rc = handler_check(handler);

    if (EXIT_SUCCESS != rc)
        return rc;

    if (NULL == list->list)
        return ERROR_HANDLER_LIST_INVALID_ITEM;

    return list_push_back(list->list, handler);
}

int handler_list_find(handler_list_t *list, const request_t *const request,
                      handler_call_t *const call)
{
    if (NULL == list || NULL == request || NULL == call)
        return ERROR_HANDLER_LIST_NULL;

    if (NULL == list->list)
        return ERROR_HANDLER_LIST_INVALID_ITEM;

    handler_t *found;
    list_filter_t filter = {handler_find, request};
    int rc = list_find(list->list, &filter, (void **)&found);

    if (EXIT_SUCCESS != rc)
        return ERROR_HANDLER_LIST_INVALID_ITEM;

    if (NULL == found)
        return ERROR_HANDLER_LIST_NOT_FOUND;

    call->function = found->function;
    call->arg = found->arg;

    return EXIT_SUCCESS;
}

void handler_list_free(handler_list_t **list)
{
    if (NULL == list || NULL == *list)
        return;

    list_iterator_t *iter = list_begin((*list)->list),
                    *end = list_end((*list)->list);

    if (NULL != iter && NULL != end)
    {
        for (; list_iterator_ne(end, iter); list_iterator_next(iter))
        {
            handler_t *handler = list_iterator_get(iter);

            if (handler && handler->free_callback)
                handler->free_callback(&handler->arg);
        }
    }

    list_iterator_free(&iter);
    list_iterator_free(&end);

    list_free(&(*list)->list);
    free(*list);
    *list = NULL;
}

static int handler_check(const handler_t *const handler)
{
    if (NULL == handler)
        return ERROR_HANDLER_LIST_NULL;

    if (NULL == handler->check || NULL == handler->function)
        return ERROR_HANDLER_LIST_INVALID_HANDLER;

    return EXIT_SUCCESS;
}

static int handler_find(const void *const arg, const void *const value)
{
    if (NULL == arg || NULL == value)
        return 0;

    return ((const handler_t *)value)->check((const request_t *)arg);
}

