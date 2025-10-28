#include "config.h"

#include "multiplexer.h"
#include "threads.h"
#include "epoll.h"
#include "pselect.h"

static int check_multiplexer(const struct multiplexer_variant *config);
static multiplexer_t *setup_multiplexer(const struct multiplexer_variant *config);

static int check_runner(const struct runner_variant *config);
static server_t *setup_runner(const struct runner_variant *config);
static server_t *setup_threads_runner(const struct threads_server_config *config);
static server_t *setup_prefork_runner(const struct prefork_server_config *config);

struct config config_default(void)
{
    struct config out = {
        .addr = INADDR_ANY,
        .cwd = NULL,
        .level = WARNING,
        .port = 80,
        .runner = {
            .type = PREFORK,
            .variant = {
                .prefork = {
                    .processes = 4,
                    .multiplexer = {
                        .type = EPOLL,
                        .connection_timeout = 5000,
                        .poll_timeout = 500,
                    },
                },
            },
        },
    };
    return out;
}

int config_check(const struct config *config)
{
    if (NULL == config) {
        return ERROR_CONFIG_NULL;
    }

    if (0 >= config->port) {
        return ERROR_CONFIG_NEGATIVE_PORT;
    }

    return check_runner(&config->runner);
}

server_config_t config_setup_server(const struct config *config)
{
    server_config_t out = {
        .addr = config->addr,
        .port = config->port,
        .handlers = NULL,
        .runner = setup_runner(&config->runner),
    };

    return out;
}

static int check_runner(const struct runner_variant *config)
{
    switch (config->type) {
    case THREADS:
        if (0 == config->variant.threads.workers) {
            return ERROR_CONFIG_ZERO_WORKERS;
        }
        return check_multiplexer(&config->variant.threads.multiplexer);
    case PREFORK:
        if (0 == config->variant.prefork.processes) {
            return ERROR_CONFIG_ZERO_WORKERS;
        }
        return check_multiplexer(&config->variant.prefork.multiplexer);
    default:
        return ERROR_CONFIG_INVALID_SERVER_VARIANT;
    }
}

static server_t *setup_runner(const struct runner_variant *config)
{
    switch (config->type) {
    case THREADS:
        return setup_threads_runner(&config->variant.threads);
    case PREFORK:
        return setup_prefork_runner(&config->variant.prefork);
    default:
        errno = ERROR_CONFIG_UNCHECKED;
        return NULL;
    }
}

static server_t *setup_threads_runner(const struct threads_server_config *config)
{
    multiplexer_t *multiplexer = setup_multiplexer(&config->multiplexer);
    if (NULL == multiplexer) {
        return NULL;
    }

    threads_server_config_t server_config = {
        .connection_timeout = config->multiplexer.connection_timeout,
        .multiplexer_timeout = config->multiplexer.poll_timeout,
        .max_threads = config->workers,
        .multiplexer = multiplexer,
    };
    return threads_server_init(&server_config);
}

static server_t *setup_prefork_runner(const struct prefork_server_config *_)
{
    return NULL;
}

static int check_multiplexer(const struct multiplexer_variant *config)
{
    switch (config->type) {
    case PSELECT:
    case EPOLL:
        return EXIT_SUCCESS;
    default:
        return ERROR_CONFIG_INVALID_MULTIPLEXER_VARIANT;;
    }
}

static multiplexer_t *setup_multiplexer(const struct multiplexer_variant *config)
{
    switch (config->type) {
    case PSELECT:
        return pselect_multiplexer();
    case EPOLL:
        return epoll_multiplexer();
    default:
        errno = ERROR_CONFIG_UNCHECKED;
        return NULL;
    }
}

