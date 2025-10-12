#ifndef _MULTIPLEXER_H_
#define _MULTIPLEXER_H_

#include <stdlib.h>
#include <errno.h>

#include "list.h"

#define ERROR_MULTIPLEXER_NULL            1
#define ERROR_MULTIPLEXER_INVALID         1
#define ERROR_MULTIPLEXER_ALLOCATION      1
#define ERROR_MULTIPLEXER_MUTEX_INIT      1
#define ERROR_MULTIPLEXER_MUTEX           1
#define ERROR_MULTIPLEXER_LIST_OPERATION  1
#define ERROR_MULTIPLEXER_SELECT_ERROR    1
#define ERROR_MULTIPLEXER_OVERFLOW        1
#define ERROR_MULTIPLEXER_INTERNAL        1

enum
{
    READ  = 1,
    WRITE = 2
};

typedef int (*multiplexer_wait_func)(void *multiplexer, list_t *ready, size_t timeout);
typedef int (*multiplexer_add_func)(void *multiplexer, int socket, int status, size_t timeout);
typedef int (*multiplexer_timeout_func)(void *multiplexer, list_t *deleted);
typedef int (*multiplexer_remove_func)(void *multiplexer, int socket);
typedef int (*multiplexer_clear_func)(void *multiplexer);
typedef void (*multiplexer_free_func)(void **multiplexer);

typedef struct _multiplexer multiplexer_t;

multiplexer_t *multiplexer_init(void *multiplexer, multiplexer_wait_func wait,
        multiplexer_add_func add, multiplexer_timeout_func timeout,
        multiplexer_remove_func remove, multiplexer_clear_func clear,
        multiplexer_free_func free);

int multiplexer_wait(multiplexer_t *multiplexer, list_t *ready, size_t timeout);

int multiplexer_add(multiplexer_t *multiplexer, int socket, int status, size_t timeout);
int multiplexer_timeout(multiplexer_t *multiplexer, list_t *deleted);
int multiplexer_remove(multiplexer_t *multiplexer, int socket);
int multiplexer_clear(multiplexer_t *multiplexer);

void multiplexer_free(multiplexer_t **multiplexer);

#endif

