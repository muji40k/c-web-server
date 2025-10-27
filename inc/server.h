#ifndef _SERVER_H_
#define _SERVER_H_

#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <netinet/in.h>

#include "handler.h"

#define ERROR_SERVER_NULL 1
#define ERROR_SERVER_CONFIG 1
#define ERROR_SERVER_NEGATIVE_PORT 1
#define ERROR_SERVER_NOT_SETUP 1
#define ERROR_SERVER_ALLOCATION 1
#define ERROR_SERVER_MULTIPLEXING 1
#define ERROR_SERVER_LOCK 1
#define ERROR_SERVER_LIST 1
#define ERROR_SERVER_ACCEPT 1
#define ERROR_SERVER_WRITE 1
#define ERROR_SERVER_CLOSE 1

typedef struct _server server_t;

typedef int (*server_mainloop_func_t)(void *runner, int listen_fd, handler_list_t *handlers);
typedef int (*server_terminate_func_t)(void *runner);
typedef void (*server_free_func_t)(void **runner);

typedef struct {
    int port;
    in_addr_t addr;
    server_t *runner;
    handler_list_t *handlers;
} server_config_t;

server_t *server_init(void *runner,
    server_mainloop_func_t mainloop_func,
    server_terminate_func_t terminate_func,
    server_free_func_t free_func
);
void server_free(server_t **server);

int server_setup(void);
void server_destroy(void);
void server_termination_handler(int signum);

int serve(const server_config_t *config);

#endif

