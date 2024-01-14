#include "server.h"

#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <memory.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <fcntl.h>

#include "logger.h"

#include "multiplexer.h"
#include "worker.h"
#include "list.h"

#define TIMEOUT_MULTIPLEXER 500
#define TIMEOUT_CONNECTION  5000
// #define TIMEOUT_CONNECTION  0

static pthread_mutex_t stat_mutex;
static size_t accepted = 0;
static size_t correct = 0;
static size_t error = 0;
static size_t rejected = 0;
static size_t refused = 0;
static size_t timeout = 0;

struct _server
{
    int init;
    int port;
    size_t timeout;
    size_t max_threads;
    worker_t *workers;
    handler_list_t *list;
    multiplexer_t *multiplexer;
};

typedef struct
{
    const server_t *server;
    int run;
} server_status_t;

static int setup = 0;
static list_t *servers = NULL;
static int setup_mutex = 0;
static pthread_mutex_t mutex;

static int find_status_by_server(const void *const arg, const void *const value);

static server_status_t *status_register(const server_t *const server);
static int status_drop(const server_status_t *const status);

static int setup_threads(server_t *server);
static int stop_threads(server_t *server);

static int worker_callback(void *arg, int socket);
static void worker_callback_init(server_t *server, worker_callback_t *callback);
static void worker_error_func(void *arg, int socket, int error);
static void worker_error_init(worker_error_t *error);

static int server_refuse_connection(int socket);

int server_setup(void)
{
    if (setup)
        return EXIT_SUCCESS;

    int rc = EXIT_SUCCESS;

    servers = list_init(sizeof(server_status_t));

    if (NULL == servers)
        rc = ERROR_SERVER_ALLOCATION;

    if (EXIT_SUCCESS == rc)
        rc = pthread_mutex_init(&mutex, NULL);

    pthread_mutex_init(&stat_mutex, NULL);

    if (EXIT_SUCCESS != rc)
        server_destroy();
    else
    {
        setup = 1;
        setup_mutex = 1;
    }

    return rc;
}

void server_destroy(void)
{
    if (!setup)
        return;

    if (servers)
        list_free(&servers);

    if (setup_mutex)
        pthread_mutex_destroy(&mutex);

    setup = 0;
    setup_mutex = 0;
}

void server_termination_handler(int signum)
{
    if (SIGINT != signum)
        return;

    if (NULL == servers)
        return;

    if (EXIT_SUCCESS != pthread_mutex_lock(&mutex))
        return;

    list_iterator_t *iter = list_begin(servers), *end = list_end(servers);
    server_status_t *status = NULL;

    for (; list_iterator_ne(iter, end); list_iterator_next(iter))
    {
        status = list_iterator_get(iter);

        if (status)
            status->run = 0;
    }

    list_iterator_free(&iter);
    list_iterator_free(&end);

    pthread_mutex_unlock(&mutex);
}

server_t *server_init(int port, size_t max_threads)
{
    if (0 == max_threads)
        return errno = ERROR_SERVER_ZERO_THREADS, NULL;

    if (0 > port)
        return errno = ERROR_SERVER_NEGATIVE_PORT, NULL;

    server_t *server = malloc(sizeof(server_t));

    if (NULL == server)
        return errno = ERROR_SERVER_ALLOCATION, NULL;

    server->init = 0;
    server->port = port;
    server->timeout = TIMEOUT_CONNECTION;
    server->max_threads = max_threads;
    server->workers = NULL;
    server->list = NULL;
    server->multiplexer = NULL;

    server->workers = malloc(worker_size() * max_threads);
    int rc = EXIT_SUCCESS;

    if (NULL == server->workers)
        rc = ERROR_SERVER_ALLOCATION;

    if (EXIT_SUCCESS == rc)
    {
        server->list = handler_list_init();

        if (NULL == server->list)
            rc = errno;
    }

    if (EXIT_SUCCESS == rc)
    {
        server->multiplexer = multiplexer_init();

        if (NULL == server->multiplexer)
            rc = errno;
    }

    if (EXIT_SUCCESS != rc)
        server_free(&server);

    return errno = rc, server;
}

int server_set_timeout(server_t *const server, size_t timeout)
{
    if (NULL == server)
        return ERROR_SERVER_NULL;

    server->timeout = timeout;

    return EXIT_SUCCESS;
}

int server_register_handler(server_t *const server,
                            const handler_t *const handler)
{
    if (NULL == server)
        return ERROR_SERVER_NULL;

    return handler_list_push(server->list, handler);
}

static int server_process_connections(server_t *const server,
                                      const int listen_fd,
                                      list_t *const ready)
{
    int rc = EXIT_SUCCESS;

    list_iterator_t *iter = list_begin(ready), *end = list_end(ready);

    if (NULL == iter || NULL == end)
    {
        LOG_M(ERROR, "Unable to access socket pool");
        rc = ERROR_SERVER_ALLOCATION;
    }

    for (; EXIT_SUCCESS == rc && list_iterator_ne(iter, end);
         list_iterator_next(iter))
    {
        int *socket = list_iterator_get(iter);

        if (NULL == socket)
        {
            LOG_M(ERROR, "Unable to access socket");
            rc = ERROR_SERVER_LIST;
        }
        else
        {
            if (listen_fd != *socket)
            {
                LOG_F(INFO, "Socket %d: ready", *socket);
                int drc = worker_request_dispatch(server->workers,
                                                  server->max_threads,
                                                  *socket);

                if (EXIT_SUCCESS != drc)
                {
                    LOG_F(WARNING, "Socket %d: connection refused", *socket);
                    rc = server_refuse_connection(*socket);

                    if (EXIT_SUCCESS != close(*socket))
                        rc = rc ? rc : ERROR_SERVER_CLOSE;

                    pthread_mutex_lock(&stat_mutex);
                    refused++;
                    pthread_mutex_unlock(&stat_mutex);
                }

                int rrc = multiplexer_remove(server->multiplexer, *socket);

                if (EXIT_SUCCESS == rc && EXIT_SUCCESS == drc && EXIT_SUCCESS == rrc)
                    LOG_F(INFO, "Socket %d: dispatched and removed from pool", *socket);
                else if (EXIT_SUCCESS != rc)
                    LOG_F(ERROR, "Socket %d: unable to refuse", *socket);
                else if (EXIT_SUCCESS != rrc)
                {
                    LOG_F(ERROR, "Socket %d: unable to remove socket from pool", *socket);
                    rc = rrc;
                }
            }
            else
            {
                int conn_fd = accept(listen_fd, NULL, NULL);

                if (-1 != conn_fd)
                {
                    LOG_F(INFO, "New connection: %d", conn_fd);
                    pthread_mutex_lock(&stat_mutex);
                    accepted++;
                    pthread_mutex_unlock(&stat_mutex);
                }
                else
                {
                    LOG_M(ERROR, "Unable to accept connection");
                    rc = ERROR_SERVER_ACCEPT;
                }

                if (EXIT_SUCCESS == rc)
                {
                    rc = multiplexer_add(server->multiplexer, conn_fd,
                                         READ | WRITE, server->timeout);

                    if (ERROR_MULTIPLEXER_OVERFLOW == rc)
                    {
                        LOG_F(ERROR, "Socket %d: unable to add to pool. Overflow", conn_fd);
                        // rc = server_refuse_connection(conn_fd);
                        if (EXIT_SUCCESS != close(conn_fd))
                            rc = ERROR_SERVER_CLOSE;
                        else
                            rc = EXIT_SUCCESS;

                        pthread_mutex_lock(&stat_mutex);
                        rejected++;
                        pthread_mutex_unlock(&stat_mutex);
                    }
                    else if (EXIT_SUCCESS != rc)
                    {
                        LOG_F(ERROR, "Socket %d: Unable to add to pool. Internal error", conn_fd);
                        // server_refuse_connection(conn_fd);
                        if (EXIT_SUCCESS != close(conn_fd))
                            rc = ERROR_SERVER_CLOSE;
                        else
                            rc = EXIT_SUCCESS;

                        pthread_mutex_lock(&stat_mutex);
                        rejected++;
                        pthread_mutex_unlock(&stat_mutex);
                    }
                }
            }
        }
    }

    list_iterator_free(&iter);
    list_iterator_free(&end);

    return rc;
}

static int server_process_timeout(list_t *const deleted)
{
    int rc = EXIT_SUCCESS;

    list_iterator_t *iter = list_begin(deleted), *end  = list_end(deleted);

    if (NULL == iter || NULL == end)
    {
        LOG_M(ERROR, "Unable to access socket pool");
        rc = ERROR_SERVER_ALLOCATION;
    }

    for (; EXIT_SUCCESS == rc && list_iterator_ne(iter, end);
         list_iterator_next(iter))
    {
        int *socket = list_iterator_get(iter);

        if (NULL != socket)
        {
            LOG_F(WARNING, "Socket %d: timeout", *socket);
            server_refuse_connection(*socket);
        }

        if (NULL == socket)
        {
            LOG_M(ERROR, "Unable to access socket");
            rc = ERROR_SERVER_LIST;
        }
        else if (EXIT_SUCCESS != close(*socket))
            rc = ERROR_SERVER_CLOSE;

        pthread_mutex_lock(&stat_mutex);
        timeout++;
        pthread_mutex_unlock(&stat_mutex);
    }

    list_iterator_free(&iter);
    list_iterator_free(&end);

    return rc;
}

int server_mainloop(server_t *const server)
{
    if (NULL == server)
    {
        LOG_M(ERROR, "Null passed as server reference");

        return ERROR_SERVER_NULL;
    }

    if (!setup)
    {
        LOG_M(ERROR, "Server wasn't setup before mainloop");

        return ERROR_SERVER_NOT_SETUP;
    }

    int rc = setup_threads(server);

    int listen_fd = 0;
    struct sockaddr_in serv_addr;
    server_status_t *status = NULL;
    list_t *sockets = NULL;

    if (EXIT_SUCCESS == rc)
    {
        status = status_register(server);

        if (NULL == status)
        {
            LOG_M(ERROR, "Unable to register server watcher");
            rc = errno;
        }
    }

    if (EXIT_SUCCESS == rc)
    {
        sockets = list_init(sizeof(int));

        if (NULL == sockets)
        {
            LOG_M(ERROR, "Unable to allocate list of sockets");
            rc = errno;
        }
    }

    if (EXIT_SUCCESS == rc
        && (-1 == (listen_fd = socket(AF_INET, SOCK_STREAM, 0))))
    {
        LOG_M(ERROR, "Unable to create socket");
        rc = EXIT_FAILURE;
    }

    if (EXIT_SUCCESS == rc)
    {
        int flags = fcntl(listen_fd, F_GETFL, 0);
        flags |= O_NONBLOCK;
        fcntl(listen_fd, F_SETFL, flags);
    }

    if (EXIT_SUCCESS == rc)
    {
        int on = 1;
        rc = setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    }

    if (EXIT_SUCCESS == rc)
    {
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        serv_addr.sin_port = htons(server->port);
    }

    if (EXIT_SUCCESS == rc
        && (-1 == bind(listen_fd, (struct sockaddr *)&serv_addr,
                       sizeof(serv_addr))))
    {
        LOG_M(ERROR, "Unable to bind socket");
        rc = EXIT_FAILURE;
    }

    if (EXIT_SUCCESS == rc && (-1 == listen(listen_fd, SOMAXCONN)))
    {
        LOG_M(ERROR, "Unable to mark socket as listen");
        rc = EXIT_FAILURE;
    }

    if (EXIT_SUCCESS == rc)
    {
        rc = multiplexer_add(server->multiplexer, listen_fd, READ, 0);

        if (EXIT_SUCCESS != rc)
        {
            LOG_F(ERROR, "Unable to add socket %d to pool", listen_fd);
            rc = ERROR_SERVER_MULTIPLEXING;
        }
    }

    if (EXIT_SUCCESS == rc)
        LOG_M(INFO, "Server up");

    while (EXIT_SUCCESS == rc && status->run)
    {
        rc = multiplexer_wait(server->multiplexer, sockets, TIMEOUT_MULTIPLEXER);

        // Process ready
        if (EXIT_SUCCESS == rc)
            rc = server_process_connections(server, listen_fd, sockets);

        // Remove timeout
        if (EXIT_SUCCESS == rc)
            rc = multiplexer_timeout(server->multiplexer, sockets);

        if (EXIT_SUCCESS == rc)
            rc = server_process_timeout(sockets);

        // Wake up workers
        if (EXIT_SUCCESS == rc)
            rc = worker_wake_up(server->workers, server->max_threads);
    }

    if (0 != listen_fd)
        close(listen_fd);

    multiplexer_clear(server->multiplexer);
    list_free(&sockets);

    if (status)
    {
        int crc = status_drop(status);

        if (EXIT_SUCCESS == rc)
            rc = crc;

        if (EXIT_SUCCESS != crc)
            LOG_M(ERROR, "Unable to drop server watcher");
    }

    printf("\n"
           "Accepted: %zu\n"
           "Correct:  %zu\n"
           "Rejected: %zu\n"
           "Refused: %zu\n"
           "Timeout:  %zu\n"
           "Sum:      %zu\n"
           "Error:    %zu\n",
           accepted, correct, rejected, refused, timeout,
           correct + rejected + timeout + refused,
           error);

    LOG_M(INFO, "Server down");

    return rc;
}

void server_free(server_t **const server)
{
    if (NULL == server || NULL == *server)
        return;

    stop_threads(*server);
    handler_list_free(&(*server)->list);
    multiplexer_free(&(*server)->multiplexer);
    free((*server)->workers);
    free(*server);

    *server = NULL;
}

static int find_status_by_server(const void *const arg, const void *const value)
{
    if (NULL == arg || NULL == value)
        return 0;

    return (server_t *)arg == ((server_status_t *)value)->server;
}

static server_status_t *status_register(const server_t *const server)
{
    int rc = EXIT_SUCCESS;
    server_status_t status_new = {server, 1};
    int rclock = pthread_mutex_lock(&mutex);
    server_status_t *status = NULL;

    if (EXIT_SUCCESS == rclock)
        rc = list_push_back(servers, &status_new);

    if (EXIT_SUCCESS != rc)
        rc = ERROR_SERVER_ALLOCATION;

    if (EXIT_SUCCESS == rc)
    {
        list_filter_t filter = {find_status_by_server, server};
        rc = list_find(servers, &filter, (void **)&status);
    }

    if (EXIT_SUCCESS == rclock)
        rclock = pthread_mutex_unlock(&mutex);

    if (EXIT_SUCCESS != rclock)
        rc = ERROR_SERVER_LOCK;

    if (EXIT_SUCCESS != rc)
        return errno = rc, NULL;

    return status;
}

static int status_drop(const server_status_t *const status)
{
    int rc = EXIT_SUCCESS, rclock = pthread_mutex_lock(&mutex);

    if (EXIT_SUCCESS == rclock)
        rc = list_remove_single(servers, status);

    if (EXIT_SUCCESS == rclock)
        rclock = pthread_mutex_unlock(&mutex);

    if (EXIT_SUCCESS != rclock && EXIT_SUCCESS == rc)
        rc = ERROR_SERVER_LOCK;

    return rc;
}

static int setup_threads(server_t *server)
{
    int rc = EXIT_SUCCESS;

    if (server->init)
        rc = stop_threads(server);

    char *base = (char *)server->workers;
    size_t size = worker_size();
    worker_callback_t callback;
    worker_error_t error;
    worker_callback_init(server, &callback);
    worker_error_init(&error);

    for (size_t i = 0; EXIT_SUCCESS == rc && server->max_threads > i; i++)
        rc = worker_init((void *)(base + i * size), server->list, &callback,
                         &error);

    if (EXIT_SUCCESS == rc)
        server->init = 1;

    return rc;
}

static int stop_threads(server_t *server)
{
    if (!server->init)
        return EXIT_SUCCESS;

    char *base = (char *)server->workers;
    size_t size = worker_size();

    for (size_t i = 0; server->max_threads > i; i++)
        worker_destroy((worker_t *)(base + i * size));

    int rc = multiplexer_clear(server->multiplexer);

    if (EXIT_SUCCESS != rc)
        rc = ERROR_SERVER_MULTIPLEXING;

    server->init = 0;

    return rc;
}

static int worker_callback(void *arg, int socket)
{
    if (NULL == arg)
        return ERROR_WORKER_NULL;

    int rc = EXIT_SUCCESS;

    if (0 <= socket && EXIT_SUCCESS != close(socket))
        rc = ERROR_SERVER_CLOSE;

    pthread_mutex_lock(&stat_mutex);
    correct++;
    pthread_mutex_unlock(&stat_mutex);

    return rc;
}

static void worker_callback_init(server_t *server, worker_callback_t *callback)
{
    callback->func = worker_callback;
    callback->arg = server->multiplexer;
}

#define FORM                                 \
"HTTP/1.1 %d %s\r\n"                         \
"Content-Type: text/html; charset=UTF-8\r\n" \
"\r\n"                                       \
"<html>"                                     \
    "<head>"                                 \
        "<title>Error occured</title>"       \
    "</head>"                                \
    "<body>"                                 \
        "<h1>Error</h1>"                     \
        "<p>%s</p>"                          \
    "</body>"                                \
"</html>"

static void worker_error_func(void *arg, int socket, int error)
{
    if (NULL != arg)
        return;

    int code = 500;
    const char *msg = "Internal Server Error";
    const char *desc = "Unexpected error";

    switch (error)
    {
        case (WORKER_ERROR_WRONG_ACTION):
            code = 501;
            msg  = "Not Implemented";
            desc = "Server can't process such request";
            break;
        case (WORKER_ERROR_READ):
        case (WORKER_ERROR_WRONG_READ):
        case (WORKER_ERROR_INVALID_ACTION):
        case (WORKER_ERROR_IN_ACTION):
        case (WORKER_ERROR_LOCK):
        case (WORKER_ERROR_CALLBACK):
        case (WORKER_ERROR_ALLOCAION):
            desc = "Server internal error";
            break;
    }

    ssize_t len = snprintf(NULL, 0, FORM, code, msg, desc);
    char *buffer = malloc(len + 1);

    if (!buffer)
        return;

    pthread_mutex_lock(&stat_mutex);
    error++;
    pthread_mutex_unlock(&stat_mutex);

    snprintf(buffer, len + 1, FORM, code, msg, desc);
    send(socket, buffer, len, 0);
    free(buffer);
}

static void worker_error_init(worker_error_t *error)
{
    error->func = worker_error_func;
    error->arg = NULL;
}

#define REFUSE_MESSAGE                                                  \
"HTTP/1.1 503 Service Unavailable\r\n"                                  \
"Content-Type: text/html; charset=UTF-8\r\n"                            \
"\r\n"                                                                  \
"<html>"                                                                \
    "<head>"                                                            \
        "<title>Resource Busy</title>"                                  \
    "</head>"                                                           \
    "<body>"                                                            \
        "<h1>Resource Busy</h1>"                                        \
        "<p>Your request cannot be completed at this time. Please try " \
           "again later.</p>"                                           \
    "</body>"                                                           \
"</html>"

static int server_refuse_connection(int socket)
{
    static ssize_t len = -1;

    if (-1 == len)
        len = strlen(REFUSE_MESSAGE);

    int rc = EXIT_SUCCESS;

    if (len != send(socket, REFUSE_MESSAGE, len, 0))
        rc = ERROR_SERVER_WRITE;

    return rc;
}

