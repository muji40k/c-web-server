#define _POSIX_C_SOURCE 200112L
#include "multiplexer.h"

#include <sys/select.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>

#include "list_misc.h"

typedef struct
{
    int fd;
    int status;
    struct timeval entered;
    size_t timeout;
} socket_status_t;

struct _multiplexer
{
    list_t *sockets;
    pthread_mutex_t mutex;
    int mutex_init;
};

static int multiplexer_check(const multiplexer_t *multiplexer);
static int multiplexer_wait_clear(list_t *ready);
static int multiplexer_wait_main(const multiplexer_t *multiplexer, list_t *ready,
                                 const size_t timeout);

multiplexer_t *multiplexer_init(void)
{
    multiplexer_t *out = malloc(sizeof(multiplexer_t));
    int rc = EXIT_SUCCESS;

    if (!out)
        rc = ERROR_MULTIPLEXER_ALLOCATION;

    if (EXIT_SUCCESS == rc)
    {
        out->mutex_init = 0;
        out->sockets = list_init(sizeof(socket_status_t));

        if (!out->sockets)
            rc = ERROR_MULTIPLEXER_ALLOCATION;
    }

    if (EXIT_SUCCESS == rc)
    {
        rc = pthread_mutex_init(&out->mutex, NULL);

        if (EXIT_SUCCESS != rc)
            rc = ERROR_MULTIPLEXER_MUTEX_INIT;
        else
            out->mutex_init = 1;
    }

    if (EXIT_SUCCESS != rc)
    {
        multiplexer_free(&out);

        return errno = rc, NULL;
    }

    return out;
}

int multiplexer_wait(multiplexer_t *const multiplexer, list_t *ready,
                     const size_t timeout)
{
    int rc = multiplexer_check(multiplexer);

    if (EXIT_SUCCESS != rc)
        return rc;

    if (NULL == ready)
        return ERROR_MULTIPLEXER_NULL;

    rc = pthread_mutex_lock(&multiplexer->mutex);

    if (EXIT_SUCCESS != rc)
        rc = ERROR_MULTIPLEXER_MUTEX;

    if (EXIT_SUCCESS == rc)
        rc = multiplexer_wait_clear(ready);

    if (EXIT_SUCCESS == rc)
        rc = multiplexer_wait_main(multiplexer, ready, timeout);

    int mrc = pthread_mutex_unlock(&multiplexer->mutex);

    if (EXIT_SUCCESS != mrc && EXIT_SUCCESS == rc)
        rc = ERROR_MULTIPLEXER_MUTEX;

    return rc;
}

int multiplexer_add(multiplexer_t *const multiplexer, const int socket,
                    const int status, const size_t timeout)
{
    int rc = multiplexer_check(multiplexer);

    if (EXIT_SUCCESS != rc)
        return rc;

    if (0 == socket || 0 == status)
        return ERROR_MULTIPLEXER_NULL;

    if (FD_SETSIZE <= socket)
        return ERROR_MULTIPLEXER_OVERFLOW;

    int mrc = pthread_mutex_lock(&multiplexer->mutex);

    socket_status_t sstatus = {socket, status, {0, 0}, timeout};
    gettimeofday(&sstatus.entered, NULL);

    if (EXIT_SUCCESS == mrc)
        rc = list_push_back(multiplexer->sockets, &sstatus);

    if (EXIT_SUCCESS != rc)
        rc = ERROR_MULTIPLEXER_LIST_OPERATION;

    if (EXIT_SUCCESS == mrc)
        mrc = pthread_mutex_unlock(&multiplexer->mutex);

    if (EXIT_SUCCESS == rc && EXIT_SUCCESS != mrc)
        rc = ERROR_MULTIPLEXER_MUTEX;

    return rc;
}

struct timeout_handler
{
    int *rc;
    struct timeval now;
    list_t *deleted;
};

static size_t get_diff(const struct timeval *begin, const struct timeval *end)
{
    return (end->tv_sec - begin->tv_sec) * 1000
           + (end->tv_usec - begin->tv_usec) / 1000;
}

static int remove_timeout(const void *const arg, const void *const value)
{
    if (NULL == arg || NULL == value)
        return 0;

    const struct timeout_handler *handler = arg;
    const socket_status_t *status = value;

    if (EXIT_SUCCESS != *handler->rc)
        return 0;

    if (0 == status->timeout)
        return 0;

    size_t diff = get_diff(&status->entered, &handler->now);

    if (diff < status->timeout)
        return 0;

    if (EXIT_SUCCESS != list_push_back(handler->deleted, &status->fd))
        *handler->rc = ERROR_MULTIPLEXER_LIST_OPERATION;

    return 1;
}

int multiplexer_timeout(multiplexer_t *const multiplexer, list_t *deleted)
{
    int rc = multiplexer_check(multiplexer);

    if (EXIT_SUCCESS != rc)
        return rc;

    if (NULL == deleted)
        return ERROR_MULTIPLEXER_NULL;

    rc = pthread_mutex_lock(&multiplexer->mutex);

    if (EXIT_SUCCESS != rc)
        rc = ERROR_MULTIPLEXER_MUTEX;

    if (EXIT_SUCCESS == rc)
        rc = multiplexer_wait_clear(deleted);

    if (EXIT_SUCCESS == rc)
    {
        struct timeval now;
        gettimeofday(&now, NULL);
        int inrc = EXIT_SUCCESS;
        struct timeout_handler handler = {&inrc, now, deleted};
        list_filter_t filter = {remove_timeout, &handler};

        rc = list_remove(multiplexer->sockets, &filter);

        if (EXIT_SUCCESS == rc && EXIT_SUCCESS != inrc)
            rc = inrc;
    }

    int mrc = pthread_mutex_unlock(&multiplexer->mutex);

    if (EXIT_SUCCESS != mrc && EXIT_SUCCESS == rc)
        rc = ERROR_MULTIPLEXER_MUTEX;

    return rc;
}

static int remove_socket(const void *const arg, const void *const value)
{
    if (NULL == arg || NULL == value)
        return 0;

    return *(int *)arg == ((socket_status_t *)value)->fd;
}

int multiplexer_remove(multiplexer_t *const multiplexer, const int socket)
{
    int rc = multiplexer_check(multiplexer);

    if (EXIT_SUCCESS != rc)
        return rc;

    if (0 == socket)
        return ERROR_MULTIPLEXER_NULL;

    int mrc = pthread_mutex_lock(&multiplexer->mutex);

    list_filter_t filter = {remove_socket, &socket};

    if (EXIT_SUCCESS == mrc)
        rc = list_remove(multiplexer->sockets, &filter);

    if (EXIT_SUCCESS != rc)
        rc = ERROR_MULTIPLEXER_LIST_OPERATION;

    if (EXIT_SUCCESS == mrc)
        mrc = pthread_mutex_unlock(&multiplexer->mutex);

    if (EXIT_SUCCESS == rc && EXIT_SUCCESS != mrc)
        rc = ERROR_MULTIPLEXER_MUTEX;

    return rc;
}

static int close_on_remove(const void *const arg, const void *const value)
{
    if (NULL == value || NULL != arg)
        return 0;

    close(((const socket_status_t *)value)->fd);

    return 1;
}

int multiplexer_clear(multiplexer_t *const multiplexer)
{
    int rc = multiplexer_check(multiplexer);

    if (EXIT_SUCCESS != rc)
        return rc;

    list_filter_t filter = {close_on_remove, NULL};
    rc = list_remove(multiplexer->sockets, &filter);

    if (EXIT_SUCCESS != rc)
        rc = ERROR_MULTIPLEXER_LIST_OPERATION;

    return rc;
}

void multiplexer_free(multiplexer_t **multiplexer)
{
    if (NULL == multiplexer || NULL == *multiplexer)
        return;

    if ((*multiplexer)->mutex_init)
        pthread_mutex_destroy(&(*multiplexer)->mutex);

    list_free(&(*multiplexer)->sockets);
    free(*multiplexer);
    *multiplexer = NULL;
}

static int multiplexer_check(const multiplexer_t *multiplexer)
{
    if (NULL == multiplexer)
        return ERROR_MULTIPLEXER_NULL;

    if (NULL == multiplexer->sockets)
        return ERROR_MULTIPLEXER_INVALID;

    if (0 == multiplexer->mutex_init)
        return ERROR_MULTIPLEXER_INVALID;

    return EXIT_SUCCESS;
}

static int multiplexer_wait_clear(list_t *ready)
{
    list_filter_t filter;
    list_misc_init_remove_all(&filter);
    int rc = list_remove(ready, &filter);

    if (EXIT_SUCCESS != rc)
        rc = ERROR_MULTIPLEXER_LIST_OPERATION;

    return rc;
}

static int multiplexer_wait_main(const multiplexer_t *multiplexer, list_t *ready,
                                 const size_t timeout)
{
    int interrupted = 0;
    int rc = EXIT_SUCCESS;
    list_iterator_t *iter = list_begin(multiplexer->sockets),
                    *end = list_end(multiplexer->sockets);
    fd_set read, write;
    int max = 0;
    FD_ZERO(&read);
    FD_ZERO(&write);

    if (NULL == iter || NULL == end)
        rc = ERROR_MULTIPLEXER_LIST_OPERATION;

    for (; EXIT_SUCCESS == rc && list_iterator_ne(end, iter);
         list_iterator_next(iter))
    {
        socket_status_t *status = list_iterator_get(iter);

        if (NULL == status)
            rc = ERROR_MULTIPLEXER_LIST_OPERATION;

        if (EXIT_SUCCESS == rc)
        {
            if (READ & status->status)
            {
                FD_SET(status->fd, &read);
                max = max > status->fd ? max : status->fd;
            }

            if (WRITE & status->status)
            {
                FD_SET(status->fd, &write);
                max = max > status->fd ? max : status->fd;
            }
        }
    }

    list_iterator_free(&iter);

    if (EXIT_SUCCESS == rc)
    {
        struct timespec *sttimeout = NULL, buf;

        if (timeout)
        {
            buf.tv_sec = timeout / 1000;
            buf.tv_nsec = (timeout % 1000) * 1e6;
            sttimeout = &buf;
        }

        if (-1 == pselect(max + 1, &read, &write, NULL, sttimeout, NULL))
        {
            if (EINTR == errno)
                interrupted = 1;
            else
                rc = ERROR_MULTIPLEXER_SELECT_ERROR;
        }
    }

    if (!interrupted && EXIT_SUCCESS == rc)
    {
        iter = list_begin(multiplexer->sockets);

        if (NULL == iter)
            rc = ERROR_MULTIPLEXER_LIST_OPERATION;
    }

    for (; !interrupted && EXIT_SUCCESS == rc && list_iterator_ne(end, iter);
         list_iterator_next(iter))
    {
        socket_status_t *status = list_iterator_get(iter);

        if (NULL == status)
            rc = ERROR_MULTIPLEXER_LIST_OPERATION;

        if (EXIT_SUCCESS == rc)
        {
            int is_ready = 1;

            if (is_ready && READ & status->status
                && !FD_ISSET(status->fd, &read))
                is_ready = 0;

            if (is_ready && WRITE & status->status
                && !FD_ISSET(status->fd, &write))
                is_ready = 0;

            if (is_ready)
            {
                rc = list_push_back(ready, &status->fd);

                if (EXIT_SUCCESS != rc)
                    rc = ERROR_MULTIPLEXER_LIST_OPERATION;
            }
        }
    }

    list_iterator_free(&iter);
    list_iterator_free(&end);

    return rc;
}

