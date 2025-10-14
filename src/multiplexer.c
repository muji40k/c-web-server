#include "multiplexer.h"

#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>

struct _multiplexer {
    void *multiplexer;
    multiplexer_wait_func wait;
    multiplexer_add_func add;
    multiplexer_timeout_func timeout;
    multiplexer_remove_func remove;
    multiplexer_clear_func clear;
    multiplexer_free_func free;
};

static int multiplexer_check(const multiplexer_t *multiplexer);

multiplexer_t *multiplexer_init(void *multiplexer, multiplexer_wait_func wait_func,
        multiplexer_add_func add_func, multiplexer_timeout_func timeout_func,
        multiplexer_remove_func remove_func, multiplexer_clear_func clear_func,
        multiplexer_free_func free_func)
{
    if (NULL == wait_func || NULL == add_func || NULL == timeout_func
        || NULL == remove_func || NULL == clear_func || NULL == free_func) {
        return errno = ERROR_MULTIPLEXER_NULL, NULL;
    }

    multiplexer_t *out = malloc(sizeof(multiplexer_t));
    int rc = EXIT_SUCCESS;

    if (!out) {
        rc = ERROR_MULTIPLEXER_ALLOCATION;
    }

    if (EXIT_SUCCESS == rc) {
        out->multiplexer = multiplexer;
        out->add = add_func;
        out->wait = wait_func;
        out->clear = clear_func;
        out->remove = remove_func;
        out->timeout = timeout_func;
        out->free = free_func;
    }

    if (EXIT_SUCCESS != rc) {
        free(out);
        return errno = rc, NULL;
    }

    return out;
}

int multiplexer_wait(multiplexer_t *const multiplexer, list_t *ready,
    const size_t timeout)
{
    int rc = multiplexer_check(multiplexer);

    if (EXIT_SUCCESS == rc) {
        rc = multiplexer->wait(multiplexer->multiplexer, ready, timeout);
    }

    return rc;
}

int multiplexer_add(multiplexer_t *const multiplexer, const int socket,
                    const int status, const size_t timeout)
{
    int rc = multiplexer_check(multiplexer);

    if (EXIT_SUCCESS == rc) {
        rc = multiplexer->add(multiplexer->multiplexer, socket, status, timeout);
    }

    return rc;
}

int multiplexer_timeout(multiplexer_t *const multiplexer, list_t *deleted)
{
    int rc = multiplexer_check(multiplexer);

    if (EXIT_SUCCESS == rc) {
        rc = multiplexer->timeout(multiplexer->multiplexer, deleted);
    }

    return rc;
}

int multiplexer_remove(multiplexer_t *const multiplexer, const int socket)
{
    int rc = multiplexer_check(multiplexer);

    if (EXIT_SUCCESS == rc) {
        rc = multiplexer->remove(multiplexer->multiplexer, socket);
    }

    return rc;
}

int multiplexer_clear(multiplexer_t *const multiplexer)
{
    int rc = multiplexer_check(multiplexer);

    if (EXIT_SUCCESS == rc) {
        rc = multiplexer->clear(multiplexer->multiplexer);
    }

    return rc;
}

void multiplexer_free(multiplexer_t **multiplexer)
{
    if (NULL == multiplexer || NULL == *multiplexer) {
        return;
    }

    (*multiplexer)->free(&(*multiplexer)->multiplexer);
    free(*multiplexer);
    *multiplexer = NULL;
}

static int multiplexer_check(const multiplexer_t *multiplexer)
{
    if (NULL == multiplexer) {
        return ERROR_MULTIPLEXER_NULL;
    }

    if (NULL == multiplexer->wait || NULL == multiplexer->add
        || NULL == multiplexer->timeout || NULL == multiplexer->remove
        || NULL == multiplexer->clear || NULL == multiplexer->free) {
        return ERROR_MULTIPLEXER_INVALID;
    }

    return EXIT_SUCCESS;
}

