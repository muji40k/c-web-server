#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <stdlib.h>
#include <netinet/in.h>

#include "server.h"
#include "logger.h"

#define ERROR_CONFIG_NULL                        1
#define ERROR_CONFIG_UNCHECKED                   1
#define ERROR_CONFIG_NEGATIVE_PORT               1
#define ERROR_CONFIG_ZERO_WORKERS                1
#define ERROR_CONFIG_INVALID_SERVER_VARIANT      1
#define ERROR_CONFIG_INVALID_MULTIPLEXER_VARIANT 1

enum multiplexer_type {
    PSELECT = 1,
    EPOLL
};

struct multiplexer_variant {
    size_t connection_timeout;
    size_t poll_timeout;
    enum multiplexer_type type;
};

struct threads_server_config {
    size_t workers;
    struct multiplexer_variant multiplexer;
};

struct prefork_server_config {
    size_t processes;
    struct multiplexer_variant multiplexer;
};

union runner_config {
    struct threads_server_config threads;
    struct prefork_server_config prefork;
};

enum runner_type {
    THREADS = 1,
    PREFORK
};

struct runner_variant {
    enum runner_type type;
    union runner_config variant;
};

struct config {
    const char *cwd;

    in_addr_t addr;
    int port;

    log_level_t level;

    struct runner_variant runner;
};

struct config config_default(void);
int config_check(const struct config *config);
server_config_t config_setup_server(const struct config *config);

#endif // _CONFIG_H_

