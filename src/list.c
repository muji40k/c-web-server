#include "list.h"

#include <string.h>

struct _list_item;
typedef struct _list_item
{
    struct _list_item *next;
} list_item_t;

struct _list
{
    size_t item_size;
    list_item_t *begin;
    list_item_t *end;
};

struct _list_iterator
{
    list_item_t *current;
};

static int list_check(const list_t *const list);
static int list_filter_check(const list_filter_t *const filter);
static int list_item_alloc(list_item_t **item, const void *const value, const size_t size);
static void *list_item_get(list_item_t *item);
static void list_item_free(list_item_t **item);

void *list_iterator_get(list_iterator_t *const iter)
{
    if (NULL == iter || NULL == iter->current)
        return errno = ERROR_LIST_ITERATOR_NULL, NULL;

    return list_item_get(iter->current);
}

int list_iterator_next(list_iterator_t *const iter)
{
    if (NULL == iter)
        return ERROR_LIST_ITERATOR_NULL;

    if (iter->current)
        iter->current = iter->current->next;

    return EXIT_SUCCESS;
}

int list_iterator_ne(const list_iterator_t *const iter1,
                     const list_iterator_t *const iter2)
{
    if (NULL == iter1 || NULL == iter2)
        return 1;

    return iter1->current != iter2->current;
}

void list_iterator_free(list_iterator_t **iter)
{
    if (NULL == iter || NULL == *iter)
        return;

    free(*iter);
    *iter = NULL;
}

list_t *list_init(const size_t item_size)
{
    if (0 == item_size)
        return errno = ERROR_LIST_INVALID_SIZE, NULL;

    list_t *list = malloc(sizeof(list_t));

    if (NULL == list)
        return errno = ERROR_LIST_ALLOCATION, NULL;

    list->begin = NULL;
    list->end = NULL;
    list->item_size = item_size;

    return list;
}

int list_push_back(list_t *const list, const void *const item)
{
    if (NULL == item)
        return ERROR_LIST_NULL;

    int rc = list_check(list);

    if (EXIT_SUCCESS == rc)
    {
        if (NULL == list->end)
        {
            rc = list_item_alloc(&list->end, item, list->item_size);

            if (EXIT_SUCCESS == rc && NULL == list->begin)
                list->begin = list->end;
        }
        else
        {
            list_item_t *new = NULL;

            rc = list_item_alloc(&new, item, list->item_size);

            if (EXIT_SUCCESS == rc)
            {
                list->end->next = new;
                list->end = new;
            }
        }
    }

    return rc;
}

int list_push_front(list_t *const list, const void *const item)
{
    if (NULL == item)
        return ERROR_LIST_NULL;

    int rc = list_check(list);

    if (EXIT_SUCCESS == rc)
    {
        if (NULL == list->begin)
        {
            rc = list_item_alloc(&list->begin, item, list->item_size);

            if (EXIT_SUCCESS == rc && NULL == list->end)
                list->end = list->begin;
        }
        else
        {
            list_item_t *new = NULL;

            rc = list_item_alloc(&new, item, list->item_size);

            if (EXIT_SUCCESS == rc)
            {
                new->next = list->begin;
                list->begin = new;
            }
        }
    }

    return rc;
}

int list_find(const list_t *const list, const list_filter_t *const filter, void **const item)
{
    if (NULL == filter || NULL == item)
        return ERROR_LIST_NULL;

    int rc = list_check(list);

    if (EXIT_SUCCESS == rc)
        rc = list_filter_check(filter);

    if (EXIT_SUCCESS != rc)
        return rc;

    int found = 0;
    *item = NULL;

    for (list_item_t *current = list->begin;
         NULL != current && !found;
         current = current->next)
        if (filter->check(filter->arg, list_item_get(current)))
        {
            found = 1;
            *item = list_item_get(current);
        }

    return EXIT_SUCCESS;
}

int list_remove_single(list_t *const list, const void *const item)
{
    if (NULL == item)
        return ERROR_LIST_NULL;

    int rc = list_check(list);

    if (EXIT_SUCCESS != rc)
        return rc;

    if (NULL == list->begin)
        return EXIT_SUCCESS;

    if (item == list_item_get(list->begin))
    {
        list_item_t *tmp = list->begin;
        list->begin = tmp->next;
        list_item_free(&tmp);

        if (NULL == list->begin)
            list->end = NULL;

        return EXIT_SUCCESS;
    }

    list_item_t *previous = list->begin, *current = list->begin->next;

    for (; NULL != current && item != list_item_get(current);
         previous = current, current = current->next);

    if (NULL != current)
    {
        previous->next = current->next;
        list_item_free(&current);

        if (NULL == previous->next)
            list->end = previous;
    }

    return EXIT_SUCCESS;
}

int list_remove(list_t *const list, const list_filter_t *const filter)
{
    int rc = list_check(list);

    if (EXIT_SUCCESS == rc)
        rc = list_filter_check(filter);

    if (EXIT_SUCCESS != rc)
        return rc;

    while (NULL != list->begin
           && filter->check(filter->arg, list_item_get(list->begin)))
    {
        list_item_t *tmp = list->begin;
        list->begin = tmp->next;
        list_item_free(&tmp);
    }

    if (NULL == list->begin)
    {
        list->end = NULL;

        return EXIT_SUCCESS;
    }

    list_item_t *previous = list->begin, *current = list->begin->next;

    while (NULL != current)
    {
        if (filter->check(filter->arg, list_item_get(current)))
        {
            list_item_t *tmp = current;
            previous->next = current->next;
            current = current->next;
            list_item_free(&tmp);
        }
        else
        {
            previous = current;
            current = current->next;
        }
    }

    list->end = previous;

    return EXIT_SUCCESS;
}

list_iterator_t *list_begin(list_t *const list)
{
    int rc = list_check(list);

    if (EXIT_SUCCESS != rc)
        return errno = rc, NULL;

    list_iterator_t *out = malloc(sizeof(list_iterator_t));

    if (NULL == out)
        return errno = ERROR_LIST_ALLOCATION, NULL;

    out->current = list->begin;

    return out;
}

list_iterator_t *list_end(list_t *const list)
{
    int rc = list_check(list);

    if (EXIT_SUCCESS != rc)
        return errno = rc, NULL;

    list_iterator_t *out = malloc(sizeof(list_iterator_t));

    if (NULL == out)
        return errno = ERROR_LIST_ALLOCATION, NULL;

    out->current = NULL;

    return out;
}

void list_free(list_t **list)
{
    if (NULL == list || NULL == *list)
        return;

    for (list_item_t *current = (*list)->begin, *next = NULL;
         NULL != current;
         current = next)
    {
        next = current->next;
        list_item_free(&current);
    }

    free(*list);
    *list = NULL;
}

static int list_check(const list_t *const list)
{
    if (NULL == list)
        return ERROR_LIST_NULL;

    if (0 == list->item_size)
        return ERROR_LIST_INVALID_ITEM;

    if ((NULL == list->end && NULL != list->begin)
        || (NULL == list->begin && NULL != list->end))
        return ERROR_LIST_INVALID_ITEM;

    return EXIT_SUCCESS;
}

static int list_filter_check(const list_filter_t *const filter)
{
    if (NULL == filter || NULL == filter->check)
        return ERROR_LIST_NULL;

    return EXIT_SUCCESS;
}

static int list_item_alloc(list_item_t **item, const void *const value, const size_t size)
{
    if (NULL == item)
        return ERROR_LIST_NULL;

    *item = malloc(sizeof(list_item_t) + size);

    if (NULL == *item)
        return ERROR_LIST_ALLOCATION;

    if (value)
        memcpy(list_item_get(*item), value, size);

    (*item)->next = NULL;

    return EXIT_SUCCESS;
}

static void *list_item_get(list_item_t *item)
{
    if (NULL == item)
        return errno = ERROR_LIST_NULL, NULL;

    return item + 1;
}

static void list_item_free(list_item_t **item)
{
    if (NULL == item || NULL == *item)
        return;

    free(*item);
    *item = NULL;
}

