#include "threads.h"

#include <unistd.h>
#include <pthread.h>
#include <memory.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include "codes.h"
#include "responses.h"

#include "logger.h"

#include "worker.h"
#include "list.h"

#define TIMEOUT_MULTIPLEXER 500
#define TIMEOUT_CONNECTION  5000

typedef struct {
    int init;
    int run;
    size_t connection_timeout;
    size_t multiplexer_timeout;
    size_t max_threads;
    worker_t *workers;
    multiplexer_t *multiplexer;
} threads_server_t;

static int check_config(threads_server_config_t *config);

static int setup_threads(threads_server_t *server, handler_list_t *handlers);
static int stop_threads(threads_server_t *server);

static int mainloop_func(void *runner, int listen_fd, handler_list_t *handlers);
static int terminate_func(void *runner);
static void free_func(void **runner);

static int worker_callback(void *arg, int socket);
static void worker_callback_init(threads_server_t *server, worker_callback_t *callback);
static void worker_error_func(void *arg, int socket, int error);
static void worker_error_init(worker_error_t *error);

threads_server_config_t threads_server_config(void)
{
    threads_server_config_t config = {
        .connection_timeout = TIMEOUT_CONNECTION,
        .multiplexer_timeout = TIMEOUT_MULTIPLEXER,
        .max_threads = 1,
        .multiplexer = NULL,
    };
    return config;
}

server_t *threads_server_init(threads_server_config_t *config)
{
    int rc = check_config(config);
    if (EXIT_SUCCESS != rc) {
        return errno = rc, NULL;
    }

    threads_server_t *server = malloc(
        sizeof(threads_server_t) + worker_size() * config->max_threads
    );

    if (NULL == server) {
        return errno = ERROR_SERVER_ALLOCATION, NULL;
    }

    server->init = 0;
    server->run = 0;
    server->connection_timeout = config->connection_timeout;
    server->multiplexer_timeout = config->multiplexer_timeout;
    server->max_threads = config->max_threads;
    server->multiplexer = config->multiplexer;
    server->workers = (void *)(server + 1);
    memset(server->workers, 0, worker_size() * server->max_threads);
    server_t *out = server_init(server, mainloop_func, terminate_func, free_func);

    if (NULL == out) {
        free_func((void **)&server);
    }

    return out;
}

static int server_process_connections(threads_server_t *const server,
                                      const int listen_fd,
                                      list_t *const ready)
{
    int rc = EXIT_SUCCESS;

    list_iterator_t *iter = list_iter(ready);

    if (NULL == iter) {
        LOG_M(ERROR, "Unable to access socket pool");
        rc = ERROR_SERVER_ALLOCATION;
    }

    for (list_iterator_item_t item = list_iterator_next(iter);
         EXIT_SUCCESS == rc && item.next;
         item = list_iterator_next(iter)) {
        int *socket = item.value;

        if (NULL == socket) {
            LOG_M(ERROR, "Unable to access socket");
            rc = ERROR_SERVER_LIST;
        } else {
            if (listen_fd != *socket) {
                LOG_F(INFO, "Socket %d: ready", *socket);
                int drc = worker_request_dispatch(server->workers,
                                                  server->max_threads,
                                                  *socket);

                if (EXIT_SUCCESS != drc) {
                    LOG_F(WARNING, "Socket %d: connection refused", *socket);
                    rc = send_refuse_connection(*socket);

                    if (EXIT_SUCCESS != close(*socket)) {
                        rc = rc ? rc : ERROR_SERVER_CLOSE;
                    }
                }

                int rrc = multiplexer_remove(server->multiplexer, *socket);

                if (EXIT_SUCCESS == rc && EXIT_SUCCESS == drc && EXIT_SUCCESS == rrc) {
                    LOG_F(INFO, "Socket %d: dispatched and removed from pool", *socket);
                } else if (EXIT_SUCCESS != rc) {
                    LOG_F(ERROR, "Socket %d: unable to refuse", *socket);
                } else if (EXIT_SUCCESS != rrc) {
                    LOG_F(ERROR, "Socket %d: unable to remove socket from pool", *socket);
                    rc = rrc;
                }
            } else {
                int conn_fd = accept(listen_fd, NULL, NULL);

                if (-1 != conn_fd) {
                    LOG_F(INFO, "New connection: %d", conn_fd);
                } else {
                    LOG_M(ERROR, "Unable to accept connection");
                    rc = ERROR_SERVER_ACCEPT;
                }

                if (EXIT_SUCCESS == rc) {
                    rc = multiplexer_add(server->multiplexer, conn_fd,
                                         READ | WRITE, server->connection_timeout);

                    if (ERROR_MULTIPLEXER_OVERFLOW == rc) {
                        LOG_F(ERROR, "Socket %d: unable to add to pool. Overflow", conn_fd);
                        // rc = server_refuse_connection(conn_fd);
                        if (EXIT_SUCCESS != close(conn_fd)) {
                            rc = ERROR_SERVER_CLOSE;
                        } else {
                            rc = EXIT_SUCCESS;
                        }
                    } else if (EXIT_SUCCESS != rc) {
                        LOG_F(ERROR, "Socket %d: Unable to add to pool. Internal error", conn_fd);
                        // server_refuse_connection(conn_fd);
                        if (EXIT_SUCCESS != close(conn_fd)) {
                            rc = ERROR_SERVER_CLOSE;
                        } else {
                            rc = EXIT_SUCCESS;
                        }
                    }
                }
            }
        }
    }

    list_iterator_free(&iter);

    return rc;
}

static int server_process_timeout(list_t *const deleted)
{
    int rc = EXIT_SUCCESS;

    list_iterator_t *iter = list_iter(deleted);

    if (NULL == iter) {
        LOG_M(ERROR, "Unable to access socket pool");
        rc = ERROR_SERVER_ALLOCATION;
    }

    for (list_iterator_item_t item = list_iterator_next(iter);
         EXIT_SUCCESS == rc && item.next;
         item = list_iterator_next(iter)) {
        int *socket = item.value;

        if (NULL != socket) {
            LOG_F(WARNING, "Socket %d: timeout", *socket);
            send_refuse_connection(*socket);
        }

        if (NULL == socket) {
            LOG_M(ERROR, "Unable to access socket");
            rc = ERROR_SERVER_LIST;
        } else if (EXIT_SUCCESS != close(*socket)) {
            rc = ERROR_SERVER_CLOSE;
        }
    }

    list_iterator_free(&iter);

    return rc;
}

int mainloop_func(void *runner, int listen_fd, handler_list_t *handlers)
{
    if (NULL == runner || NULL == handlers) {
        LOG_M(ERROR, "NULL threaded server reference");
        return ERROR_SERVER_NULL;
    }

    threads_server_t *server = runner;
    int rc = setup_threads(server, handlers);
    list_t *sockets = NULL;

    if (EXIT_SUCCESS == rc) {
        sockets = list_init(sizeof(int));

        if (NULL == sockets) {
            rc = ERROR_SERVER_ALLOCATION;
        }
    }

    if (EXIT_SUCCESS == rc) {
        rc = multiplexer_add(server->multiplexer, listen_fd, READ, 0);

        if (EXIT_SUCCESS != rc) {
            LOG_F(ERROR, "Unable to add socket %d to pool", listen_fd);
            rc = ERROR_SERVER_MULTIPLEXING;
        }
    }

    if (EXIT_SUCCESS == rc) {
        LOG_M(INFO, "Server up");
        server->run = 1;
    }

    while (EXIT_SUCCESS == rc && server->run) {
        rc = multiplexer_wait(server->multiplexer, sockets, server->multiplexer_timeout);

        // Process ready
        if (EXIT_SUCCESS == rc) {
            rc = server_process_connections(server, listen_fd, sockets);
        }

        // Remove timeout
        if (EXIT_SUCCESS == rc) {
            rc = multiplexer_timeout(server->multiplexer, sockets);
        }

        if (EXIT_SUCCESS == rc) {
            rc = server_process_timeout(sockets);
        }

        // Wake up workers
        if (EXIT_SUCCESS == rc) {
            rc = worker_wake_up(server->workers, server->max_threads);
        }
    }

    list_free(&sockets);
    LOG_M(INFO, "Server down");

    return rc;
}

static int terminate_func(void *runner)
{
    if (NULL == runner) {
        LOG_M(ERROR, "Can't stop server, got NULL pointer");
        return ERROR_SERVER_NULL;
    }

    ((threads_server_t *)runner)->run = 0;

    return EXIT_SUCCESS;
}

static void free_func(void **runner)
{
    threads_server_t **server = (threads_server_t **)runner;
    if (NULL == server || NULL == *server) {
        return;
    }

    stop_threads(*server);
    free(*server);
    *server = NULL;
}

static int check_config(threads_server_config_t *config)
{
    if (NULL == config->multiplexer) {
        return ERROR_SERVER_NULL;
    }

    if (0 == config->max_threads) {
        config->max_threads = 1;
    }

    if (0 == config->connection_timeout) {
        config->connection_timeout = TIMEOUT_CONNECTION;
    }

    if (0 == config->multiplexer_timeout) {
        config->multiplexer_timeout = TIMEOUT_MULTIPLEXER;
    }

    return EXIT_SUCCESS;
}

static int setup_threads(threads_server_t *server, handler_list_t *handlers)
{
    int rc = EXIT_SUCCESS;

    if (server->init) {
        rc = stop_threads(server);
    }

    char *base = (char *)server->workers;
    size_t size = worker_size();
    worker_callback_t callback;
    worker_error_t error;
    worker_callback_init(server, &callback);
    worker_error_init(&error);

    for (size_t i = 0; EXIT_SUCCESS == rc && server->max_threads > i; i++) {
        rc = worker_init((void *)(base + i * size), handlers, &callback, &error);
    }

    if (EXIT_SUCCESS == rc) {
        server->init = 1;
    }

    return rc;
}

static int stop_threads(threads_server_t *server)
{
    if (!server->init) {
        return EXIT_SUCCESS;
    }

    char *base = (char *)server->workers;
    size_t size = worker_size();

    for (size_t i = 0; server->max_threads > i; i++) {
        worker_destroy((worker_t *)(base + i * size));
    }

    server->init = 0;

    return EXIT_SUCCESS;
}

static int worker_callback(void *arg, int socket)
{
    if (NULL == arg) {
        return ERROR_WORKER_NULL;
    }

    int rc = EXIT_SUCCESS;

    if (0 <= socket && EXIT_SUCCESS != close(socket)) {
        rc = ERROR_SERVER_CLOSE;
    }

    return rc;
}

static void worker_callback_init(threads_server_t *server, worker_callback_t *callback)
{
    callback->func = worker_callback;
    callback->arg = server->multiplexer;
}


static void worker_error_func(void *arg, int socket, int error)
{
    if (NULL != arg) {
        return;
    }

    int code = HTTP_INTERNAL_SERVER_ERROR;
    const char *msg = "Unexpected error";

    switch (error) {
    case (WORKER_ERROR_WRONG_ACTION):
        code = HTTP_NOT_IMPLEMENTED;
        msg = "Server can't process such request";
        break;
    case (WORKER_ERROR_READ):
    case (WORKER_ERROR_WRONG_READ):
    case (WORKER_ERROR_INVALID_ACTION):
    case (WORKER_ERROR_IN_ACTION):
    case (WORKER_ERROR_LOCK):
    case (WORKER_ERROR_CALLBACK):
    case (WORKER_ERROR_ALLOCAION):
    default:
        break;
    }

    send_internal_error(socket, code, msg);
}

static void worker_error_init(worker_error_t *error)
{
    error->func = worker_error_func;
    error->arg = NULL;
}

