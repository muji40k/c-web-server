#ifndef _THREADS_H_
#define _THREADS_H_

#include "server.h"
#include "multiplexer.h"

typedef struct {
    size_t connection_timeout;
    size_t multiplexer_timeout;
    size_t max_threads;
    multiplexer_t *multiplexer;
} threads_server_config_t;

threads_server_config_t threads_server_config(void);

server_t *threads_server_init(threads_server_config_t *config);

#endif // _THREADS_H_

