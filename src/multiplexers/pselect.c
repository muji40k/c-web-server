#define _POSIX_C_SOURCE 200112L
#include "pselect.h"

#include <sys/select.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>

#include "list_misc.h"
#include "misc.h"

typedef struct {
    int fd;
    int status;
    struct timeval entered;
    size_t timeout;
} socket_status_t;

typedef struct {
    list_t *sockets;
    pthread_mutex_t mutex;
    int mutex_init;
} pselect_t;

static int wait_func(void *multiplexer, list_t *ready, size_t timeout);
static int add_func(void *multiplexer, int socket, int status, size_t timeout);
static int timeout_func(void *multiplexer, list_t *deleted);
static int remove_func(void *multiplexer, int socket);
static int clear_func(void *multiplexer);
static void free_func(void **multiplexer);

static int check(const pselect_t *multiplexer);
static int wait_clear(list_t *ready);
static int wait_main(const pselect_t *multiplexer, list_t *ready, const size_t timeout);

multiplexer_t *pselect_multiplexer(void)
{
    multiplexer_t *out = NULL;
    pselect_t *inner = malloc(sizeof(pselect_t));
    int rc = EXIT_SUCCESS;

    if (!inner) {
        rc = ERROR_MULTIPLEXER_ALLOCATION;
    }

    if (EXIT_SUCCESS == rc) {
        inner->mutex_init = 0;
        inner->sockets = list_init(sizeof(socket_status_t));

        if (!inner->sockets) {
            rc = ERROR_MULTIPLEXER_ALLOCATION;
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

    if (EXIT_SUCCESS == rc) {
        out = multiplexer_init(inner, wait_func, add_func, timeout_func,
            remove_func, clear_func, free_func);

        if (NULL == out) {
            rc = errno;
        }
    }

    if (EXIT_SUCCESS != rc) {
        free_func((void **)&inner);
        return errno = rc, NULL;
    }

    return out;
}

static int wait_func(void *const wrap, list_t *ready, const size_t timeout)
{
    pselect_t *multiplexer = (pselect_t *)wrap;
    int rc = check(multiplexer);

    if (EXIT_SUCCESS != rc) {
        return rc;
    }

    if (NULL == ready) {
        return ERROR_MULTIPLEXER_NULL;
    }

    rc = pthread_mutex_lock(&multiplexer->mutex);

    if (EXIT_SUCCESS != rc) {
        rc = ERROR_MULTIPLEXER_MUTEX;
    }

    if (EXIT_SUCCESS == rc) {
        rc = wait_clear(ready);
    }

    if (EXIT_SUCCESS == rc) {
        rc = wait_main(multiplexer, ready, timeout);
    }

    int mrc = pthread_mutex_unlock(&multiplexer->mutex);

    if (EXIT_SUCCESS != mrc && EXIT_SUCCESS == rc) {
        rc = ERROR_MULTIPLEXER_MUTEX;
    }

    return rc;
}

static int add_func(void *const wrap, const int socket, const int status, const size_t timeout)
{
    pselect_t *multiplexer = (pselect_t *)wrap;
    int rc = check(multiplexer);

    if (EXIT_SUCCESS != rc) {
        return rc;
    }

    if (0 == socket || 0 == status) {
        return ERROR_MULTIPLEXER_NULL;
    }

    if (FD_SETSIZE <= socket) {
        return ERROR_MULTIPLEXER_OVERFLOW;
    }

    int mrc = pthread_mutex_lock(&multiplexer->mutex);

    socket_status_t sstatus = {socket, status, {0, 0}, timeout};
    gettimeofday(&sstatus.entered, NULL);

    if (EXIT_SUCCESS == mrc) {
        rc = list_push_back(multiplexer->sockets, &sstatus);
    }

    if (EXIT_SUCCESS != rc) {
        rc = ERROR_MULTIPLEXER_LIST_OPERATION;
    }

    if (EXIT_SUCCESS == mrc) {
        mrc = pthread_mutex_unlock(&multiplexer->mutex);
    }

    if (EXIT_SUCCESS == rc && EXIT_SUCCESS != mrc) {
        rc = ERROR_MULTIPLEXER_MUTEX;
    }

    return rc;
}

struct timeout_handler
{
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
    const socket_status_t *status = value;

    if (EXIT_SUCCESS != *handler->rc) {
        return 0;
    }

    if (0 == status->timeout) {
        return 0;
    }

    size_t diff = get_time_diff_ms(&status->entered, &handler->now);

    if (diff < status->timeout) {
        return 0;
    }

    if (EXIT_SUCCESS != list_push_back(handler->deleted, &status->fd)) {
        *handler->rc = ERROR_MULTIPLEXER_LIST_OPERATION;
    }

    return 1;
}

static int timeout_func(void *const wrap, list_t *deleted)
{
    pselect_t *multiplexer = (pselect_t *)wrap;
    int rc = check(multiplexer);

    if (EXIT_SUCCESS != rc) {
        return rc;
    }

    if (NULL == deleted) {
        return ERROR_MULTIPLEXER_NULL;
    }

    rc = pthread_mutex_lock(&multiplexer->mutex);

    if (EXIT_SUCCESS != rc) {
        rc = ERROR_MULTIPLEXER_MUTEX;
    }

    if (EXIT_SUCCESS == rc) {
        rc = wait_clear(deleted);
    }

    if (EXIT_SUCCESS == rc) {
        struct timeval now;
        gettimeofday(&now, NULL);
        int inrc = EXIT_SUCCESS;
        struct timeout_handler handler = {&inrc, now, deleted};
        list_filter_t filter = {remove_timeout, &handler};

        rc = list_remove(multiplexer->sockets, &filter);

        if (EXIT_SUCCESS == rc && EXIT_SUCCESS != inrc) {
            rc = inrc;
        }
    }

    int mrc = pthread_mutex_unlock(&multiplexer->mutex);

    if (EXIT_SUCCESS != mrc && EXIT_SUCCESS == rc) {
        rc = ERROR_MULTIPLEXER_MUTEX;
    }

    return rc;
}

static int remove_socket(const void *const arg, const void *const value)
{
    if (NULL == arg || NULL == value) {
        return 0;
    }

    return *(int *)arg == ((socket_status_t *)value)->fd;
}

static int remove_func(void *const wrap, const int socket)
{
    pselect_t *multiplexer = (pselect_t *)wrap;
    int rc = check(multiplexer);

    if (EXIT_SUCCESS != rc) {
        return rc;
    }

    if (0 == socket) {
        return ERROR_MULTIPLEXER_NULL;
    }

    int mrc = pthread_mutex_lock(&multiplexer->mutex);

    list_filter_t filter = {remove_socket, &socket};

    if (EXIT_SUCCESS == mrc) {
        rc = list_remove(multiplexer->sockets, &filter);
    }

    if (EXIT_SUCCESS != rc) {
        rc = ERROR_MULTIPLEXER_LIST_OPERATION;
    }

    if (EXIT_SUCCESS == mrc) {
        mrc = pthread_mutex_unlock(&multiplexer->mutex);
    }

    if (EXIT_SUCCESS == rc && EXIT_SUCCESS != mrc) {
        rc = ERROR_MULTIPLEXER_MUTEX;
    }

    return rc;
}

static int close_on_remove(const void *const arg, const void *const value)
{
    if (NULL == value || NULL != arg) {
        return 0;
    }

    close(((const socket_status_t *)value)->fd);

    return 1;
}

static int clear_func(void *const wrap)
{
    pselect_t *multiplexer = (pselect_t *)wrap;
    int rc = check(multiplexer);

    if (EXIT_SUCCESS != rc) {
        return rc;
    }

    list_filter_t filter = {close_on_remove, NULL};
    rc = list_remove(multiplexer->sockets, &filter);

    if (EXIT_SUCCESS != rc) {
        rc = ERROR_MULTIPLEXER_LIST_OPERATION;
    }

    return rc;
}

static void free_func(void **wrap)
{
    pselect_t **multiplexer = (pselect_t **)wrap;
    if (NULL == multiplexer || NULL == *multiplexer) {
        return;
    }

    if ((*multiplexer)->mutex_init) {
        pthread_mutex_destroy(&(*multiplexer)->mutex);
    }

    list_free(&(*multiplexer)->sockets);
    free(*multiplexer);
    *multiplexer = NULL;
}

static int check(const pselect_t *multiplexer)
{
    if (NULL == multiplexer) {
        return ERROR_MULTIPLEXER_NULL;
    }

    if (NULL == multiplexer->sockets) {
        return ERROR_MULTIPLEXER_INVALID;
    }

    if (0 == multiplexer->mutex_init) {
        return ERROR_MULTIPLEXER_INVALID;
    }

    return EXIT_SUCCESS;
}

static int wait_clear(list_t *ready)
{
    list_filter_t filter = list_misc_init_remove_all();
    int rc = list_remove(ready, &filter);

    if (EXIT_SUCCESS != rc) {
        rc = ERROR_MULTIPLEXER_LIST_OPERATION;
    }

    return rc;
}

static int wait_main(const pselect_t *multiplexer, list_t *ready, const size_t timeout)
{
    int interrupted = 0;
    int rc = EXIT_SUCCESS;
    list_iterator_t *iter = list_iter(multiplexer->sockets);
    fd_set read, write;
    int max = 0;
    FD_ZERO(&read);
    FD_ZERO(&write);

    if (NULL == iter) {
        rc = ERROR_MULTIPLEXER_LIST_OPERATION;
    }

    for (list_iterator_item_t item = list_iterator_next(iter);
         EXIT_SUCCESS == rc && item.next;
         item = list_iterator_next(iter)) {
        socket_status_t *status = item.value;

        if (NULL == status) {
            rc = ERROR_MULTIPLEXER_LIST_OPERATION;
        }

        if (EXIT_SUCCESS == rc) {
            if (READ & status->status) {
                FD_SET(status->fd, &read);
                max = max > status->fd ? max : status->fd;
            }

            if (WRITE & status->status) {
                FD_SET(status->fd, &write);
                max = max > status->fd ? max : status->fd;
            }
        }
    }

    list_iterator_free(&iter);

    if (EXIT_SUCCESS == rc) {
        struct timespec *sttimeout = NULL, buf;

        if (timeout) {
            buf.tv_sec = timeout / 1000;
            buf.tv_nsec = (timeout % 1000) * 1e6;
            sttimeout = &buf;
        }

        if (-1 == pselect(max + 1, &read, &write, NULL, sttimeout, NULL)) {
            if (EINTR == errno) {
                interrupted = 1;
            } else {
                rc = ERROR_MULTIPLEXER_SELECT_ERROR;
            }
        }
    }

    if (!interrupted && EXIT_SUCCESS == rc) {
        iter = list_iter(multiplexer->sockets);

        if (NULL == iter) {
            rc = ERROR_MULTIPLEXER_LIST_OPERATION;
        }
    }

    for (list_iterator_item_t item = list_iterator_next(iter);
         !interrupted && EXIT_SUCCESS == rc && item.next;
         item = list_iterator_next(iter)) {
        socket_status_t *status = item.value;

        if (NULL == status) {
            rc = ERROR_MULTIPLEXER_LIST_OPERATION;
        }

        if (EXIT_SUCCESS == rc) {
            int is_ready = 1;

            if (is_ready && READ & status->status
                && !FD_ISSET(status->fd, &read)) {
                is_ready = 0;
            }

            if (is_ready && WRITE & status->status
                && !FD_ISSET(status->fd, &write)) {
                is_ready = 0;
            }

            if (is_ready) {
                rc = list_push_back(ready, &status->fd);

                if (EXIT_SUCCESS != rc) {
                    rc = ERROR_MULTIPLEXER_LIST_OPERATION;
                }
            }
        }
    }

    list_iterator_free(&iter);

    return rc;
}

