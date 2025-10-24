#define _POSIX_C_SOURCE 200112L
#include "epoll.h"

#include <unistd.h>
#include <sys/epoll.h>
#include <sys/time.h>
#include <pthread.h>

#include "list.h"
#include "list_misc.h"
#include "misc.h"

#define EPOLL_EVENTS_SIZE 1000

typedef struct {
    int fd;
    struct timeval entered;
    size_t timeout;
} timeout_item_t;

typedef struct {
    int fd;
    list_t *timeouts;
    pthread_mutex_t mutex;
    int mutex_init;
} epoll_container_t;

static int check(epoll_container_t *container);

static int wait_func(void *multiplexer, list_t *ready, size_t timeout);
static int add_func(void *multiplexer, int socket, int status, size_t timeout);
static int timeout_func(void *multiplexer, list_t *deleted);
static int remove_func(void *multiplexer, int socket);
static int clear_func(void *multiplexer);
static void free_func(void **multiplexer);

multiplexer_t *epoll_multiplexer(void)
{
    epoll_container_t *inner = malloc(sizeof(epoll_container_t));
    if (NULL == inner) {
        return errno = ERROR_MULTIPLEXER_ALLOCATION, NULL;
    }

    int rc = EXIT_SUCCESS;
    inner->fd = 0;
    inner->mutex_init = 0;

    inner->timeouts = list_init(sizeof(timeout_item_t));
    if (NULL == inner->timeouts) {
        rc = ERROR_MULTIPLEXER_ALLOCATION;
    }

    if (EXIT_SUCCESS == rc) {
        inner->fd = epoll_create1(0);

        if (0 > inner->fd) {
            rc = ERROR_MULTIPLEXER_INTERNAL;
        }
    }

    if (EXIT_SUCCESS == rc) {
        rc = pthread_mutex_init(&inner->mutex, NULL);

        if (EXIT_SUCCESS != rc) {
            rc = ERROR_MULTIPLEXER_MUTEX_INIT;
        } else {
            inner->mutex_init = 1;
        }
    }

    if (EXIT_SUCCESS != rc) {
        free_func((void **)&inner);
        return errno = rc, NULL;
    }

    return multiplexer_init(inner, wait_func, add_func, timeout_func,
                            remove_func, clear_func, free_func);
}

static int check(epoll_container_t *container)
{
    if (NULL == container || NULL == container->timeouts
        || 0 > container->fd || !container->mutex_init) {
        return ERROR_MULTIPLEXER_NULL;
    }

    return EXIT_SUCCESS;
}

static int wait_func(void *multiplexer, list_t *ready, size_t timeout)
{
    epoll_container_t *container = multiplexer;
    int rc = check(container);

    if (EXIT_SUCCESS == rc) {
        list_filter_t remove_all = list_misc_init_remove_all();
        rc = list_remove(ready, &remove_all);
        if (EXIT_SUCCESS != rc) {
            errno = rc;
            rc = ERROR_MULTIPLEXER_LIST_OPERATION;
        }
    }

    if (EXIT_SUCCESS != rc) {
        return rc;
    }

    int mrc = pthread_mutex_lock(&container->mutex);
    if (EXIT_SUCCESS != mrc) {
        errno = mrc;
        return ERROR_MULTIPLEXER_MUTEX;
    }

    struct epoll_event events[EPOLL_EVENTS_SIZE];
    int n = epoll_wait(container->fd, events, EPOLL_EVENTS_SIZE, timeout);

    mrc = pthread_mutex_unlock(&container->mutex);
    if (EXIT_SUCCESS != mrc) {
        errno = mrc;
        return ERROR_MULTIPLEXER_MUTEX;
    }

    if (0 > n) {
        return ERROR_MULTIPLEXER_INTERNAL;
    }

    for (int i = 0; EXIT_SUCCESS == rc && n > i; i++) {
        rc = list_push_back(ready, &events[i].data.fd);
    }

    if (EXIT_SUCCESS != rc) {
        errno = rc;
        rc = ERROR_MULTIPLEXER_LIST_OPERATION;
    }

    return rc;
}

static int add_func(void *multiplexer, int socket, int status, size_t timeout)
{
    epoll_container_t *container = multiplexer;
    int rc = check(container);
    if (EXIT_SUCCESS != rc) {
        return rc;
    }

    if (0 == socket || 0 == status) {
        return ERROR_MULTIPLEXER_NULL;
    }

    uint32_t events = 0;
    if (status & READ) {
        events |= EPOLLIN;
    }
    if (status & WRITE) {
        events |= EPOLLOUT;
    }

    struct epoll_event ev = {
        .events = events,
        .data = {
            .fd = socket,
        },
    };

    errno = pthread_mutex_lock(&container->mutex);
    if (EXIT_SUCCESS != errno) {
        return ERROR_MULTIPLEXER_MUTEX;
    }

    rc = epoll_ctl(container->fd, EPOLL_CTL_ADD, socket, &ev);
    if (EXIT_SUCCESS != rc) {
        rc = ERROR_MULTIPLEXER_INTERNAL;
    }

    if (EXIT_SUCCESS == rc) {
        timeout_item_t item = {
            .fd = socket,
            .timeout = timeout,
        };
        gettimeofday(&item.entered, NULL);

        errno = list_push_back(container->timeouts, &item);
        if (EXIT_SUCCESS != errno) {
            rc = ERROR_MULTIPLEXER_LIST_OPERATION;
        }
    }

    int mrc = pthread_mutex_unlock(&container->mutex);
    if (EXIT_SUCCESS != mrc) {
        errno = mrc;
        return ERROR_MULTIPLEXER_MUTEX;
    }

    return rc;
}

struct timeout_handler
{
    int fd;
    int *rc;
    struct timeval now;
    list_t *deleted;
};

static int remove_timeout(const void *const arg, const void *const value)
{
    if (NULL == arg || NULL == value) {
        return 0;
    }

    const struct timeout_handler *handler = arg;
    const timeout_item_t *timeout = value;

    if (EXIT_SUCCESS != *handler->rc) {
        return 0;
    }

    if (0 == timeout->timeout) {
        return 0;
    }

    size_t diff = get_time_diff_ms(&timeout->entered, &handler->now);

    if (diff < timeout->timeout) {
        return 0;
    }

    if (EXIT_SUCCESS != epoll_ctl(handler->fd, EPOLL_CTL_DEL, timeout->fd, NULL)) {
        *handler->rc = ERROR_MULTIPLEXER_INTERNAL;
        return 0;
    }

    if (EXIT_SUCCESS != list_push_back(handler->deleted, &timeout->fd)) {
        *handler->rc = ERROR_MULTIPLEXER_LIST_OPERATION;
        return 0;
    }

    return 1;
}

static int timeout_func(void *multiplexer, list_t *deleted)
{
    epoll_container_t *container = multiplexer;
    int rc = check(container);
    if (EXIT_SUCCESS != rc) {
        return rc;
    }

    if (NULL == deleted) {
        return ERROR_MULTIPLEXER_NULL;
    }

    list_filter_t remove_all = list_misc_init_remove_all();
    errno = list_remove(deleted, &remove_all);
    if (EXIT_SUCCESS != errno) {
        return ERROR_MULTIPLEXER_LIST_OPERATION;
    }

    int rrc = EXIT_SUCCESS;
    struct timeout_handler handler = {
        .deleted = deleted,
        .rc = &rrc,
    };
    list_filter_t filter = {remove_timeout, &handler};
    gettimeofday(&handler.now, NULL);

    errno = pthread_mutex_lock(&container->mutex);
    if (EXIT_SUCCESS != errno) {
        return ERROR_MULTIPLEXER_MUTEX;
    }

    rc = list_remove(container->timeouts, &filter);
    if (EXIT_SUCCESS != rc) {
        errno = rc;
        rc = ERROR_MULTIPLEXER_LIST_OPERATION;
    }

    int mrc = pthread_mutex_unlock(&container->mutex);
    if (EXIT_SUCCESS != mrc) {
        errno = mrc;
        return ERROR_MULTIPLEXER_MUTEX;
    }

    if (EXIT_SUCCESS == rc && EXIT_SUCCESS != *handler.rc) {
        rc = *handler.rc;
    }

    return rc;
}

static int remove_socket(const void *const arg, const void *const value)
{
    if (NULL == arg || NULL == value) {
        return 0;
    }

    return *(int *)arg == ((timeout_item_t *)value)->fd;
}

static int remove_func(void *multiplexer, int socket)
{
    epoll_container_t *container = multiplexer;
    int rc = check(container);
    if (EXIT_SUCCESS != rc) {
        return rc;
    }

    list_filter_t filter = {remove_socket, &socket};

    errno = pthread_mutex_lock(&container->mutex);
    if (EXIT_SUCCESS != errno) {
        return ERROR_MULTIPLEXER_MUTEX;
    }

    rc = epoll_ctl(container->fd, EPOLL_CTL_DEL, socket, NULL);
    if (EXIT_SUCCESS != rc) {
        rc = ERROR_MULTIPLEXER_INTERNAL;
    }

    if (EXIT_SUCCESS == rc) {
        rc = list_remove(container->timeouts, &filter);

        if (EXIT_SUCCESS != rc) {
            errno = rc;
            rc = ERROR_MULTIPLEXER_LIST_OPERATION;
        }
    }

    int mrc = pthread_mutex_unlock(&container->mutex);
    if (EXIT_SUCCESS != mrc) {
        errno = mrc;
        return ERROR_MULTIPLEXER_MUTEX;
    }

    return rc;
}

struct clear_handler {
    int fd;
    int *rc;
};

static int clear_all(const void *const arg, const void *const value)
{
    if (NULL == arg || NULL == value) {
        return 0;
    }

    const struct clear_handler *handler = arg;
    const timeout_item_t *timeout = value;

    int rc = epoll_ctl(handler->fd, EPOLL_CTL_DEL, timeout->fd, NULL);
    if (EXIT_SUCCESS != rc && EXIT_SUCCESS != *handler->rc) {
        *handler->rc = ERROR_MULTIPLEXER_INTERNAL;
    }

    close(timeout->fd);

    return 1;
}

static int clear_func(void *multiplexer)
{
    epoll_container_t *container = multiplexer;
    int rc = check(container);
    if (EXIT_SUCCESS != rc) {
        return rc;
    }

    int hrc = EXIT_SUCCESS;
    struct clear_handler handler = {
        .fd = container->fd,
        .rc = &hrc,
    };
    list_filter_t filter = {clear_all, &handler};

    errno = pthread_mutex_lock(&container->mutex);
    if (EXIT_SUCCESS != errno) {
        return ERROR_MULTIPLEXER_MUTEX;
    }

    rc = list_remove(container->timeouts, &filter);
    if (EXIT_SUCCESS != rc) {
        errno = rc;
        rc = ERROR_MULTIPLEXER_LIST_OPERATION;
    }

    if (EXIT_SUCCESS == rc && EXIT_SUCCESS != hrc) {
        rc = hrc;
    }

    int mrc = pthread_mutex_unlock(&container->mutex);
    if (EXIT_SUCCESS != mrc) {
        errno = mrc;
        return ERROR_MULTIPLEXER_MUTEX;
    }

    return rc;
}

static void free_func(void **multiplexer)
{
    epoll_container_t **container = (epoll_container_t **)multiplexer;
    if (NULL == container || NULL == *container) {
        return;
    }

    if ((*container)->mutex_init) {
        pthread_mutex_destroy(&(*container)->mutex);
    }

    close((*container)->fd);
    list_free(&(*container)->timeouts);
    free(*container);
    *container = NULL;
}

