#ifndef _SERVER_H_
#define _SERVER_H_

#include <stdlib.h>
#include <errno.h>
#include <stdio.h>

#include "handler.h"

#define ERROR_SERVER_NULL 1
#define ERROR_SERVER_NOT_SETUP 1
#define ERROR_SERVER_ALLOCATION 1
#define ERROR_SERVER_ZERO_THREADS 1
#define ERROR_SERVER_NEGATIVE_PORT 1
#define ERROR_SERVER_MULTIPLEXING 1
#define ERROR_SERVER_LOCK 1
#define ERROR_SERVER_LIST 1
#define ERROR_SERVER_ACCEPT 1
#define ERROR_SERVER_WRITE 1
#define ERROR_SERVER_CLOSE 1

typedef struct _server server_t;

int server_setup(void);
void server_destroy(void);

void server_termination_handler(int signum);

server_t *server_init(int port, size_t max_threads);
int server_set_timeout(server_t *const server, size_t timeout);
int server_register_handler(server_t *const server,
                            const handler_t *const handler);
int server_mainloop(server_t *const server);

void server_free(server_t **const server);

#endif

