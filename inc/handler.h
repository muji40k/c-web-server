#ifndef _HANDLER_H_
#define _HANDLER_H_

#include <errno.h>

#include "request_parser.h"

#define ERROR_HANDLER_LIST_NULL 1
#define ERROR_HANDLER_LIST_ALLOCATION 1
#define ERROR_HANDLER_LIST_INVALID_ITEM 1
#define ERROR_HANDLER_LIST_INVALID_HANDLER 1
#define ERROR_HANDLER_LIST_DUPLICATE 1
#define ERROR_HANDLER_LIST_NOT_FOUND 1
#define ERROR_HANDLER_LIST_INVALID_CALL 1

typedef int (*handler_func_t)(const int fd, const request_t *const request,
                              void *arg);

typedef struct
{
    int (*check)(const request_t *const request);
    handler_func_t function;
    void *arg;
    void (*free_callback)(void **const arg);
} handler_t;

typedef struct _handler_call handler_call_t;
typedef struct _handler_list handler_list_t;

handler_call_t *handler_call_init(void);
int handler_call(const handler_call_t *const call, const int fd,
                 const request_t *const request);
void handler_call_free(handler_call_t **const call);

handler_list_t *handler_list_init(void);
int handler_list_push(handler_list_t *list, const handler_t *const handler);
int handler_list_find(handler_list_t *list, const request_t *const request,
                      handler_call_t *const call);
void handler_list_free(handler_list_t **list);

#endif

