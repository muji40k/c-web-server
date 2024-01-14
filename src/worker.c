#define _GNU_SOURCE
#include "worker.h"

#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include "logger.h"
#include "request_parser.h"

#define WLOG_F(priority, format, ...) LOG_F((priority), "[%d] " format, gettid(), __VA_ARGS__)
#define WLOG_M(priority, msg) LOG_F((priority), "[%d] " msg, gettid())

#define INITIAL_SIZE 4096

struct _worker
{
    int sock_pair[2];
    int alive;
    int active;
    int error;
    pthread_t thread;
    pthread_mutex_t mutex;
    handler_list_t *head;
    worker_callback_t callback;
    worker_error_t ecallback;
};

size_t worker_size(void)
{
    return sizeof(struct _worker);
}

int worker_init(worker_t *worker, handler_list_t *handlers,
                worker_callback_t *callback, worker_error_t *error)
{
    if (NULL == worker || NULL == handlers || NULL == callback
        || NULL == callback->func)
        return ERROR_WORKER_NULL;

    worker->active = 0;
    worker->alive = 1;
    worker->error = 0;
    worker->head = handlers;
    worker->thread = 0;
    worker->sock_pair[0] = 0;
    worker->sock_pair[1] = 0;
    worker->callback = *callback;

    if (error)
        worker->ecallback = *error;
    else
    {
        worker->ecallback.func = NULL;
        worker->ecallback.arg = NULL;
    }

    int rc = socketpair(AF_UNIX, SOCK_STREAM, 0, worker->sock_pair);

    if (EXIT_SUCCESS != rc)
        rc = ERROR_WORKER_SOCKET_INIT;

    if (EXIT_SUCCESS == rc)
    {
        rc = pthread_create(&worker->thread, NULL, worker_main, worker);

        if (EXIT_SUCCESS != rc)
            rc = ERROR_WORKER_THREAD_INIT;
    }

    if (EXIT_SUCCESS == rc)
    {
        rc = pthread_mutex_init(&worker->mutex, NULL);

        if (EXIT_SUCCESS != rc)
            rc = ERROR_WORKER_MUTEX_INIT;
    }

    if (EXIT_SUCCESS != rc)
        worker_destroy(worker);

    return rc;
}

int worker_is_alive(worker_t *worker)
{
    if (NULL == worker)
        return ERROR_WORKER_NULL;

    return worker->alive;
}

int worker_is_active(worker_t *worker)
{
    if (NULL == worker)
        return ERROR_WORKER_NULL;

    return worker->active;
}

int worker_error(worker_t *worker)
{
    if (NULL == worker)
        return ERROR_WORKER_NULL;

    return worker->error;
}

int worker_request(worker_t *worker, const int fd)
{
    if (NULL == worker || 0 == fd)
        return ERROR_WORKER_NULL;

    LOG_F(INFO, "Request in %d", fd);

    int rc = pthread_mutex_lock(&worker->mutex);

    if (EXIT_SUCCESS != rc)
    {
        LOG_M(ERROR, "Worker mutex error");

        return ERROR_WORKER_LOCK;
    }

    if (!worker->alive)
    {
        LOG_M(ERROR, "Attempt to request on dead worker");
        rc =  ERROR_WORKER_NOT_ALIVE;
    }

    if (EXIT_SUCCESS == rc && worker->active)
    {
        LOG_M(ERROR, "Attempt to request on active worker");
        rc = ERROR_WORKER_ACTIVE;
    }

    if (EXIT_SUCCESS == rc)
    {
        ssize_t size = write(worker->sock_pair[0], &fd, sizeof(int));

        if (sizeof(int) != size)
        {
            LOG_M(ERROR, "Error during request write");
            rc = ERROR_WORKER_UNABLE_TO_WRITE;
        }
    }

    if (EXIT_SUCCESS != pthread_mutex_unlock(&worker->mutex)
        && EXIT_SUCCESS == rc)
    {
        LOG_M(ERROR, "Worker mutex error");
        rc = ERROR_WORKER_LOCK;
    }

    if (EXIT_SUCCESS == rc)
        LOG_M(INFO, "Request success");

    return rc;
}

int worker_request_dispatch(worker_t *worker, const size_t size, const int fd)
{
    int rc = EXIT_SUCCESS;
    worker_t *chosen = NULL;

    for (size_t i = 0; EXIT_SUCCESS == rc && !chosen && size > i; i++)
    {
        worker_t *current = worker + i;

        rc = pthread_mutex_lock(&current->mutex);

        if (EXIT_SUCCESS != rc)
            rc = ERROR_WORKER_LOCK;

        if (EXIT_SUCCESS == rc && current->alive && !current->active)
            chosen = current;

        if (EXIT_SUCCESS == rc)
        {
            rc = pthread_mutex_unlock(&current->mutex);

            if (EXIT_SUCCESS != rc)
                rc = ERROR_WORKER_LOCK;
        }

        if (EXIT_SUCCESS == rc && chosen)
        {
            rc = worker_request(chosen, fd);

            if (ERROR_WORKER_NOT_ALIVE == rc || ERROR_WORKER_ACTIVE == rc)
            {
                chosen = NULL;
                rc = EXIT_SUCCESS;
            }
        }
    }

    if (EXIT_SUCCESS == rc && !chosen)
        rc = ERROR_WORKER_OVERLOAD;

    return rc;
}

int worker_wake_up(worker_t *worker, const size_t size)
{
    int rc = EXIT_SUCCESS;


    for (size_t i = 0; EXIT_SUCCESS == rc && size > i; i++)
    {
        worker_t *current = worker + i;
        int mrc = pthread_mutex_lock(&current->mutex);

        if (EXIT_SUCCESS == mrc && !worker->alive)
        {
            LOG_F(INFO, "Worker %zu[%d] down", i, worker->thread);

            worker->active = 0;
            worker->alive = 1;
            worker->error = 0;
            worker->thread = 0;

            rc = pthread_create(&worker->thread, NULL, worker_main, worker);

            if (EXIT_SUCCESS == rc)
                LOG_F(INFO, "Worker %zu[%d] wake up attempt success", i, worker->thread);
            else
            {
                LOG_F(INFO, "Worker %zu[%d] wake up attempt fail", i, worker->thread);
                rc = ERROR_WORKER_THREAD_INIT;
            }
        }

        if (EXIT_SUCCESS == mrc)
        {
            mrc = pthread_mutex_unlock(&current->mutex);

            if (EXIT_SUCCESS == rc && EXIT_SUCCESS != mrc)
                rc = ERROR_WORKER_LOCK;
        }

        if (EXIT_SUCCESS != mrc)
            LOG_M(ERROR, "Mutex error");
    }

    return rc;
}

void *worker_main(void *arg)
{
    if (NULL == arg)
        pthread_exit(NULL);

    WLOG_M(INFO, "Worker start");

    worker_t *worker = arg;
    request_t *request = request_blank(INITIAL_SIZE);
    handler_call_t *call = handler_call_init();
    int fd = -1;

    if (NULL == request || NULL == call)
    {
        WLOG_M(ERROR, "Worker allocation error, down");
        request_free(&request);
        handler_call_free(&call);
        worker->error = WORKER_ERROR_ALLOCAION;

        pthread_exit(worker);
    }

    for (int rc = EXIT_SUCCESS, rclock = EXIT_SUCCESS, crc = EXIT_SUCCESS;
         worker->alive;)
    {
        ssize_t size = read(worker->sock_pair[1], &fd, sizeof(int));

        WLOG_F(INFO, "New request: %d", fd);

        if (EXIT_SUCCESS == rc && sizeof(int) != size)
        {
            if (-1 == size)
            {
                WLOG_M(ERROR, "Read error");
                worker->error = WORKER_ERROR_READ;
            }
            else
            {
                WLOG_M(ERROR, "Unexpected read error");
                worker->error = WORKER_ERROR_WRONG_READ;
            }

            fd = -1;
            rc = EXIT_FAILURE;
        }
        else if (EXIT_SUCCESS == rc && -1 == fd)
        {
            WLOG_M(INFO, "Exit signal caught");
            rc = EXIT_FAILURE;
        }

        if (EXIT_SUCCESS == rc)
            rclock = pthread_mutex_lock(&worker->mutex);

        if (EXIT_SUCCESS == rc && EXIT_SUCCESS == rclock)
        {
            worker->active = 1;
            rclock = pthread_mutex_unlock(&worker->mutex);
        }

        if (EXIT_SUCCESS == rc && EXIT_SUCCESS == rclock)
        {
            rc = request_read_exist(request, fd);

            if (EXIT_SUCCESS != rc)
            {
                worker->error = WORKER_ERROR_WRONG_READ;
                rc = EXIT_FAILURE;
            }
        }

        if (EXIT_SUCCESS == rc && EXIT_SUCCESS == rclock)
        {
            rc = handler_list_find(worker->head, request, call);

            if (ERROR_HANDLER_LIST_NOT_FOUND == rc)
            {
                WLOG_M(WARNING, "Request can't be satisfied");
                const request_title_t *title = request_title(request);

                if (title)
                    WLOG_F(DEBUG, "Request: %s %s %s", title->method,
                           title->path, title->version);

                worker->error = WORKER_ERROR_WRONG_ACTION;
                rc = EXIT_FAILURE;
            }
            else if (EXIT_SUCCESS != rc)
            {
                WLOG_M(WARNING, "Request error");
                worker->error = WORKER_ERROR_INVALID_ACTION;
            }
            else if (EXIT_SUCCESS != (rc = handler_call(call, fd, request)))
            {
                WLOG_M(WARNING, "Error during request");
                worker->error = WORKER_ERROR_IN_ACTION;
            }
        }

        if (EXIT_SUCCESS != rc && -1 != fd && worker->ecallback.func)
            worker->ecallback.func(worker->ecallback.arg, fd, worker->error);

        crc = worker->callback.func(worker->callback.arg, fd);

        if (EXIT_SUCCESS != crc)
        {
            WLOG_M(WARNING, "Callback error");
            worker->error = WORKER_ERROR_CALLBACK;
        }

        if ((EXIT_SUCCESS != rc || EXIT_SUCCESS != crc)
            && EXIT_SUCCESS == rclock)
        {
            rclock = pthread_mutex_lock(&worker->mutex);

            if (EXIT_SUCCESS == rclock)
            {
                if (-1 == fd)
                    worker->alive = 0;

                crc = rc = EXIT_SUCCESS;
                rclock = pthread_mutex_unlock(&worker->mutex);
            }
        }

        if (EXIT_SUCCESS == rc && EXIT_SUCCESS == crc
            && EXIT_SUCCESS == rclock)
        {
            WLOG_M(INFO, "Request processed correctly");
            rclock = pthread_mutex_lock(&worker->mutex);

            if (EXIT_SUCCESS == rclock)
            {
                worker->active = 0;
                rclock = pthread_mutex_unlock(&worker->mutex);
            }
        }

        if (EXIT_SUCCESS != rclock)
        {
            WLOG_M(INFO, "Mutex error");
            worker->active = 0;
            worker->alive = 0;
            worker->error = WORKER_ERROR_LOCK;
        }
    }

    request_free(&request);
    handler_call_free(&call);

    pthread_exit(worker);
}

void worker_destroy(worker_t *worker)
{
    if (NULL == worker)
        return;

    if (0 != worker->thread)
    {
        int fd = -1;
        ssize_t size = write(worker->sock_pair[0], &fd, sizeof(int));

        if (-1 != size)
            pthread_join(worker->thread, NULL);
    }

    pthread_mutex_destroy(&worker->mutex);

    for (size_t i = 0; 2 > i; i++)
        if (0 != worker->sock_pair[i])
            close(worker->sock_pair[i]);

    worker->alive = 0;
    worker->active = 0;
    worker->thread = 0;
    worker->error = 0;
    worker->head = NULL;
}

