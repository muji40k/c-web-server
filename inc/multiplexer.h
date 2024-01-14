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

enum
{
    READ  = 1,
    WRITE = 2
};

typedef struct _multiplexer multiplexer_t;

multiplexer_t *multiplexer_init(void);

int multiplexer_wait(multiplexer_t *const multiplexer, list_t *ready,
                     const size_t timeout);

int multiplexer_add(multiplexer_t *const multiplexer, const int socket,
                    const int status, const size_t timeout);
int multiplexer_timeout(multiplexer_t *const multiplexer, list_t *deleted);
int multiplexer_remove(multiplexer_t *const multiplexer, const int socket);
int multiplexer_clear(multiplexer_t *const multiplexer);

void multiplexer_free(multiplexer_t **multiplexer);

#endif

