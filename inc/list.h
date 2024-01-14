#ifndef _LIST_H_
#define _LIST_H_

#include <stdlib.h>
#include <errno.h>

#define ERROR_LIST_ITERATOR_NULL 1
#define ERROR_LIST_NULL 1
#define ERROR_LIST_ALLOCATION 1
#define ERROR_LIST_INVALID_ITEM 1
#define ERROR_LIST_INVALID_SIZE 1

typedef struct _list list_t;
typedef struct _list_iterator list_iterator_t;

typedef struct
{
    int (*check)(const void *const arg, const void *const value);
    const void *arg;
} list_filter_t;

void *list_iterator_get(list_iterator_t *const iter);
int list_iterator_next(list_iterator_t *const iter);
int list_iterator_ne(const list_iterator_t *const iter1,
                     const list_iterator_t *const iter2);
void list_iterator_free(list_iterator_t **iter);

list_t *list_init(const size_t item_size);
int list_push_back(list_t *const list, const void *const item);
int list_push_front(list_t *const list, const void *const item);
int list_find(const list_t *const list, const list_filter_t *const filter, void **const item);
int list_remove_single(list_t *const list, const void *const item);
int list_remove(list_t *const list, const list_filter_t *const filter);

list_iterator_t *list_begin(list_t *const list);
list_iterator_t *list_end(list_t *const list);

void list_free(list_t **list);

#endif

