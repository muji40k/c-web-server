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

#include "list.h"

struct _server {
    void *runner;
    server_mainloop_func_t mainloop_func;
    server_terminate_func_t terminate_func;
    server_free_func_t free_func;
};

static int setup = 0;
static list_t *servers = NULL;
static int setup_mutex = 0;
static pthread_mutex_t mutex;

static int status_register(const server_t *const server);
static int status_drop(const server_t *const server);

static int server_config_check(const server_config_t *config);

int server_setup(void)
{
    if (setup) {
        return EXIT_SUCCESS;
    }

    int rc = EXIT_SUCCESS;

    servers = list_init(sizeof(server_t*));

    if (NULL == servers) {
        rc = ERROR_SERVER_ALLOCATION;
    }

    if (EXIT_SUCCESS == rc) {
        rc = pthread_mutex_init(&mutex, NULL);
    }

    if (EXIT_SUCCESS != rc) {
        server_destroy();
    } else {
        setup = 1;
        setup_mutex = 1;
    }

    return rc;
}

void server_destroy(void)
{
    if (!setup) {
        return;
    }

    if (servers) {
        list_free(&servers);
    }

    if (setup_mutex) {
        pthread_mutex_destroy(&mutex);
    }

    setup = 0;
    setup_mutex = 0;
}

void server_termination_handler(int signum)
{
    if (SIGINT != signum) {
        return;
    }

    if (NULL == servers) {
        return;
    }

    if (EXIT_SUCCESS != pthread_mutex_lock(&mutex)) {
        return;
    }

    list_iterator_t *iter = list_iter(servers);

    for (list_iterator_item_t item = list_iterator_next(iter);
         item.next; item = list_iterator_next(iter)) {
        server_t *server = *(server_t **)item.value;

        server->terminate_func(server->runner);
    }

    list_iterator_free(&iter);

    pthread_mutex_unlock(&mutex);
}

server_t *server_init(void *runner,
    server_mainloop_func_t mainloop_func,
    server_terminate_func_t terminate_func,
    server_free_func_t free_func
)
{
    if (NULL == mainloop_func || NULL == terminate_func) {
        return errno = ERROR_SERVER_NULL, NULL;
    }

    server_t *server = malloc(sizeof(server_t));
    if (NULL == server) {
        return errno = ERROR_SERVER_ALLOCATION, NULL;
    }

    server->runner = runner;
    server->mainloop_func = mainloop_func;
    server->terminate_func = terminate_func;
    server->free_func = free_func;

    return server;
}

void server_free(server_t **server)
{
    if (NULL == server || NULL == *server) {
        return;
    }

    if (NULL != (*server)->free_func) {
        (*server)->free_func(&(*server)->runner);
    }
    free(*server);
    *server = NULL;
}


int serve(const server_config_t *config)
{
    int rc = server_config_check(config);
    if (EXIT_SUCCESS != rc) {
        errno = rc;
        return ERROR_SERVER_CONFIG;
    }

    if (!setup) {
        LOG_M(ERROR, "Server wasn't setup before calling mainloop");
        return ERROR_SERVER_NOT_SETUP;
    }

    int listen_fd = 0;
    struct sockaddr_in serv_addr;

    rc = status_register(config->runner);
    if (EXIT_SUCCESS != rc) {
        LOG_M(ERROR, "Unable to register server watcher");
        rc = errno;
    }

    if (EXIT_SUCCESS == rc
        && (-1 == (listen_fd = socket(AF_INET, SOCK_STREAM, 0)))) {
        LOG_M(ERROR, "Unable to create socket");
        rc = EXIT_FAILURE;
    }

    if (EXIT_SUCCESS == rc) {
        int flags = fcntl(listen_fd, F_GETFL, 0);
        flags |= O_NONBLOCK;
        fcntl(listen_fd, F_SETFL, flags);
    }

    if (EXIT_SUCCESS == rc) {
        int on = 1;
        rc = setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    }

    if (EXIT_SUCCESS == rc) {
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = htonl(config->addr);
        serv_addr.sin_port = htons(config->port);
    }

    if (EXIT_SUCCESS == rc
        && (-1 == bind(listen_fd, (struct sockaddr *)&serv_addr,
                       sizeof(serv_addr)))) {
        LOG_M(ERROR, "Unable to bind socket");
        rc = EXIT_FAILURE;
    }

    if (EXIT_SUCCESS == rc && (-1 == listen(listen_fd, SOMAXCONN))) {
        LOG_M(ERROR, "Unable to mark socket as listen");
        rc = EXIT_FAILURE;
    }

    if (EXIT_SUCCESS == rc) {
        LOG_M(INFO, "Listen socket ready, starting server");
        server_t *runner = config->runner;
        rc = runner->mainloop_func(runner->runner, listen_fd, config->handlers);
    }


    if (0 != listen_fd) {
        close(listen_fd);
    }

    int crc = status_drop(config->runner);

    if (EXIT_SUCCESS == rc) {
        rc = crc;
    }

    if (EXIT_SUCCESS != crc) {
        LOG_M(ERROR, "Unable to drop server watcher");
    }

    LOG_M(INFO, "Server down");

    return rc;
}

static int status_register(const server_t *const server)
{
    int rc = EXIT_SUCCESS;
    int rclock = pthread_mutex_lock(&mutex);

    if (EXIT_SUCCESS == rclock) {
        rc = list_push_back(servers, &server);

        if (EXIT_SUCCESS != rc) {
            errno = rc;
            rc = ERROR_SERVER_LIST;
        }

        rclock = pthread_mutex_unlock(&mutex);
    }

    if (EXIT_SUCCESS != rclock && EXIT_SUCCESS == rc) {
        rc = ERROR_SERVER_LOCK;
    }

    return rc;
}

static int status_drop(const server_t *const server)
{
    int rc = EXIT_SUCCESS, rclock = pthread_mutex_lock(&mutex);

    if (EXIT_SUCCESS == rclock) {
        rc = list_remove_single(servers, server);

        if (EXIT_SUCCESS != rc) {
            errno = rc;
            rc = ERROR_SERVER_LIST;
        }

        rclock = pthread_mutex_unlock(&mutex);
    }

    if (EXIT_SUCCESS != rclock && EXIT_SUCCESS == rc) {
        rc = ERROR_SERVER_LOCK;
    }

    return rc;
}

static int server_config_check(const server_config_t *config)
{
    if (NULL == config->handlers || NULL == config->runner) {
        return ERROR_SERVER_NULL;
    }

    if (0 > config->port) {
        return ERROR_SERVER_NEGATIVE_PORT;
    }

    return EXIT_SUCCESS;
}

