#define _POSIX_C_SOURCE 1
#include "request_parser.h"

#include <string.h>
#include <sys/socket.h>

#include "list.h"
#include "list_misc.h"

#define INITIAL_SIZE 4096

struct _request
{
    char *base;
    size_t size;

    list_t *headers;
    list_t *parameters;
    char *body;
    request_title_t title;
};

typedef struct
{
    const char *key;
    const char *value;
} request_item_t;

typedef struct
{
    const char *key;
    const char *value;
} parameter_item_t;

static int request_check(const request_t *const request);
static int request_read_inner(request_t *const request, const int socket, ssize_t *const size);
static int request_parse(request_t *const request, const ssize_t size);

request_t *request_blank(const size_t size)
{
    if (0 == size)
        return errno = ERROR_REQUEST_PARSER_ZERO_SIZE, NULL;

    request_t *out = malloc(sizeof(request_t));

    if (!out)
        return errno = ERROR_REQUEST_PARSER_ALLOCATION, NULL;

    out->headers = NULL;
    out->base = NULL;
    out->body = NULL;
    out->title.method = NULL;
    out->title.path = NULL;
    out->title.version = NULL;
    out->size = size;

    int rc = EXIT_SUCCESS;
    out->base = calloc(size, sizeof(char));

    if (!out->base)
        rc = ERROR_REQUEST_PARSER_ALLOCATION;

    if (EXIT_SUCCESS == rc)
    {
        out->headers = list_init(sizeof(request_item_t));

        if (NULL == out->headers)
            rc = ERROR_REQUEST_PARSER_ALLOCATION;
    }

    if (EXIT_SUCCESS == rc)
    {
        out->parameters = list_init(sizeof(parameter_item_t));

        if (NULL == out->headers)
            rc = ERROR_REQUEST_PARSER_ALLOCATION;
    }

    if (EXIT_SUCCESS != rc)
    {
        request_free(&out);
        errno = rc;
    }

    return out;
}

request_t *request_read(const int socket)
{
    request_t *out = request_blank(INITIAL_SIZE);

    if (!out)
        return NULL;

    int rc = request_read_exist(out, socket);

    if (EXIT_SUCCESS != rc)
    {
        request_free(&out);
        errno = rc;
    }

    return out;
}

int request_read_exist(request_t *request, const int socket)
{
    int rc = request_check(request);

    if (EXIT_SUCCESS != rc)
        return rc;

    if (0 > socket)
        return ERROR_REQUEST_PARSER_INVALID_SOCKET;

    ssize_t size = 0;
    list_filter_t filter;
    list_misc_init_remove_all(&filter);
    rc = list_remove(request->headers, &filter);

    if (EXIT_SUCCESS != rc)
        rc = ERROR_REQUEST_PARSER_CLEAR;

    if (EXIT_SUCCESS == rc)
        rc = request_read_inner(request, socket, &size);

    if (EXIT_SUCCESS == rc)
        rc = request_parse(request, size);

    return rc;
}

static int pfind_by_key(const void *const arg, const void *const value)
{
    if (NULL == arg || NULL == value)
        return 0;

    return !strcmp((char *)arg, ((parameter_item_t *)value)->key);
}

static int find_by_key(const void *const arg, const void *const value)
{
    if (NULL == arg || NULL == value)
        return 0;

    return !strcmp((char *)arg, ((request_item_t *)value)->key);
}

static void init_find_by_key(list_filter_t *filter, const char *const name)
{
    filter->check = find_by_key;
    filter->arg = name;
}

static void init_pfind_by_key(list_filter_t *filter, const char *const name)
{
    filter->check = pfind_by_key;
    filter->arg = name;
}

const request_title_t *request_title(const request_t *const request)
{
    int rc = request_check(request);

    if (EXIT_SUCCESS != rc)
        return errno = rc, NULL;

    return &request->title;
}

const char *request_at(const request_t *const request, const char *const name)
{
    int rc = request_check(request);

    if (EXIT_SUCCESS != rc)
        return errno = rc, NULL;

    if (NULL == name)
        return errno = ERROR_REQUEST_PARSER_NULL, NULL;

    list_filter_t filter;
    init_find_by_key(&filter, name);
    request_item_t *item = NULL;

    rc = list_find(request->headers, &filter, (void **)&item);
    errno = rc;

    if (EXIT_SUCCESS == rc)
        return item->value;

    return NULL;
}

const char *request_pararmeters_at(const request_t *const request, const char *const name)
{
    int rc = request_check(request);

    if (EXIT_SUCCESS != rc)
        return errno = rc, NULL;

    if (NULL == name)
        return errno = ERROR_REQUEST_PARSER_NULL, NULL;

    list_filter_t filter;
    init_pfind_by_key(&filter, name);
    request_item_t *item = NULL;

    rc = list_find(request->headers, &filter, (void **)&item);
    errno = rc;

    if (EXIT_SUCCESS == rc)
        return item->value;

    return NULL;
}

const char *request_body(const request_t *const request)
{
    int rc = request_check(request);

    if (EXIT_SUCCESS != rc)
        return errno = rc, NULL;

    return request->body;
}

void request_free(request_t **const request)
{
    if (NULL == request || NULL == *request)
        return;

    list_free(&(*request)->headers);
    list_free(&(*request)->parameters);
    free((*request)->base);
    free(*request);
    *request = NULL;
}

static int request_check(const request_t *const request)
{
    if (NULL == request)
        return ERROR_REQUEST_PARSER_NULL;

    if (NULL == request->base || NULL == request->headers
        || NULL == request->parameters || 0 == request->size)
        return ERROR_REQUEST_PARSER_INVALID;

    return EXIT_SUCCESS;
}

static int request_read_inner(request_t *const request, const int socket, ssize_t *const size)
{
    int rc = EXIT_SUCCESS, exp = 1;
    ssize_t insize = 0;
    size_t psize = request->size;
    size_t newsize = request->size;

    char *current = request->base;
    size_t rest = newsize;
    size_t res_size = 0;

    while (EXIT_SUCCESS == rc && exp)
    {
        insize = recv(socket, current, rest, 0);

        if (-1 == insize)
            rc = ERROR_REQUEST_PARSER_READ_ERROR;
        else
            res_size += insize;

        if (insize != (ssize_t)rest)
            exp = 0;
        else
        {
            psize = newsize;
            newsize *= 2;
            char *tmp = realloc(request->base, newsize);

            if (NULL == tmp)
                rc = ERROR_REQUEST_PARSER_ALLOCATION;
            else
            {
                rest = psize;
                request->base = tmp;
                current = request->base + psize;
                request->size = newsize;
            }
        }
    }

    if (EXIT_SUCCESS == rc)
        *size = res_size;

    return rc;
}

static int request_parse(request_t *const request, const ssize_t size)
{
    request->base[size] = 0;

    char *current = NULL, *parameters = NULL;
    char *end = request->base + size;
    char *saveptr = NULL;
    int rc = EXIT_SUCCESS;

    // Parse title
    current = strtok_r(request->base, " ", &saveptr);

    if (NULL == current)
        rc = ERROR_REQUEST_PARSER_INCORRECT;

    if (EXIT_SUCCESS == rc)
    {
        request->title.method = current;
        current = strtok_r(NULL, " ", &saveptr);

        if (NULL == current)
            rc = ERROR_REQUEST_PARSER_INCORRECT;
    }

    if (EXIT_SUCCESS == rc)
    {
        request->title.path = current;
        parameters = current;
        current = strtok_r(NULL, "\n", &saveptr);

        if (NULL == current)
            rc = ERROR_REQUEST_PARSER_INCORRECT;
    }

    if (EXIT_SUCCESS == rc)
    {
        for (; 0 != *parameters && '?' != *parameters; parameters++);

        if (0 != *parameters)
        {
            *(parameters++) = 0;
            parameter_item_t item;

            while (EXIT_SUCCESS == rc && 0 != *parameters)
            {
                item.key = parameters;

                for (; 0 != *parameters && '&' != *parameters
                       && '=' != *parameters; ++parameters);

                if ('=' != *parameters)
                    rc = ERROR_REQUEST_PARSER_INCORRECT;
                else
                {
                    *(parameters++) = 0;
                    item.value = parameters;
                }

                if (EXIT_SUCCESS == rc)
                {
                    for (; 0 != *parameters && '&' != *parameters;
                         ++parameters);

                    if ('&' == *parameters)
                        *(parameters++) = 0;
                }

                if (EXIT_SUCCESS == rc)
                {
                    rc = list_push_back(request->parameters, &item);

                    if (EXIT_SUCCESS != rc)
                        rc = ERROR_REQUEST_PARSER_ALLOCATION;
                }
            }
        }
    }

    if (EXIT_SUCCESS == rc)
    {
        size_t size = strlen(current);

        if ('\r' != current[size - 1])
            rc = ERROR_REQUEST_PARSER_INCORRECT;
        else
        {
            current[size - 1] = 0;
            request->title.version = current;
            current += size + 1;
        }
    }

    if (EXIT_SUCCESS != rc)
        return rc;

    // Parse headers
    int empty = 0;
    request_item_t item = {NULL, NULL};

    do
    {
        item.key = current;

        if ('\r' == *current && '\n' == *(current + 1))
        {
            empty = 1;
            current += 2;
        }

        for (; !empty && EXIT_SUCCESS == rc && ':' != *current; current++)
            if (end == current
                || ('\r' == *current && '\n' == *(current + 1)))
                rc = ERROR_REQUEST_PARSER_INCORRECT;

        if (!empty && EXIT_SUCCESS == rc)
            *current++ = 0;

        for (; !empty && EXIT_SUCCESS == rc && ' ' == *current; current++);

        if (!empty && EXIT_SUCCESS == rc)
            item.value = current;

        for (; !empty && EXIT_SUCCESS == rc && '\n' != *current; current++)
            if (end == current)
                rc = ERROR_REQUEST_PARSER_INCORRECT;

        if (!empty && EXIT_SUCCESS == rc && '\r' != *(current - 1))
            rc = ERROR_REQUEST_PARSER_INCORRECT;
        else if (!empty)
            *(current++ - 1) = 0;

        if (EXIT_SUCCESS == rc)
        {
            rc = list_push_back(request->headers, &item);

            if (EXIT_SUCCESS != rc)
                rc = ERROR_REQUEST_PARSER_ALLOCATION;
        }
    }
    while (EXIT_SUCCESS == rc && !empty);

    if (EXIT_SUCCESS != rc)
        return rc;

    // Body
    request->body = current;

    return rc;
}

