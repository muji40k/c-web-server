#ifndef _WORKER_H_
#define _WORKER_H_

#include <stdlib.h>

#include "handler.h"

#define ERROR_WORKER_NULL 1
#define ERROR_WORKER_SOCKET_INIT 1
#define ERROR_WORKER_THREAD_INIT 1
#define ERROR_WORKER_MUTEX_INIT 1
#define ERROR_WORKER_NOT_ALIVE 1
#define ERROR_WORKER_ACTIVE 1
#define ERROR_WORKER_UNABLE_TO_WRITE 1
#define ERROR_WORKER_LOCK 1
#define ERROR_WORKER_OVERLOAD 1

#define WORKER_ERROR_READ           1
#define WORKER_ERROR_WRONG_READ     2
#define WORKER_ERROR_WRONG_ACTION   3
#define WORKER_ERROR_INVALID_ACTION 4
#define WORKER_ERROR_IN_ACTION      5
#define WORKER_ERROR_LOCK           6
#define WORKER_ERROR_CALLBACK       7
#define WORKER_ERROR_ALLOCAION      8

typedef struct _worker worker_t;

typedef struct
{
    void *arg;
    int (*func)(void *arg, int socket);
} worker_callback_t;

typedef struct
{
    void *arg;
    void (*func)(void *arg, int socket, int error);
} worker_error_t;

size_t worker_size(void);

int worker_init(worker_t *worker, handler_list_t *handlers,
                worker_callback_t *callback, worker_error_t *error);
int worker_is_alive(worker_t *worker);
int worker_is_active(worker_t *worker);
int worker_error(worker_t *worker);
int worker_request(worker_t *worker, const int fd);
int worker_request_dispatch(worker_t *worker, const size_t size, const int fd);
int worker_wake_up(worker_t *worker, const size_t size);
void *worker_main(void *arg);
void worker_destroy(worker_t *worker);

#endif

