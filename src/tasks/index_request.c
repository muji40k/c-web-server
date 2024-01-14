#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200112L
#include "index_request.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <dirent.h>

#include <errno.h>

#include "logger.h"

#define FORM                                                              \
"HTTP/1.1 200 OK\r\n"                                                     \
"Content-Type: text/html; charset=UTF-8\r\n"                              \
"\r\n"                                                                    \
"<html>"                                                                  \
    "<head>"                                                              \
        "<title>Static server</title>"                                    \
        "<link rel=\"stylesheet\" type=\"text/css\" href=\"/index.css\">" \
    "</head>"                                                             \
    "<body>"                                                              \
        "<h1>Index of: %s</h1>"                                           \
        "<ul>"                                                            \
            "%s"                                                          \
        "</ul>"                                                           \
    "</body>"                                                             \
"</html>"
#define ITEM "<li><a href=\"%s/%s\">%s</a></li>"
#define ITEMR "<li><a href=\"%s%s\">%s</a></li>"
#define ITEMD "<li><a href=\"%s/%s/\">%s/</a></li>"
#define ITEMDR "<li><a href=\"%s%s/\">%s/</a></li>"
#define START_SIZE 1000

static int check(const request_t *const request)
{
    if (NULL == request)
        return 0;

    const request_title_t *title = request_title(request);

    if (NULL == title || strcmp("GET", title->method))
        return 0;

    struct stat path_stat;

    if (EXIT_SUCCESS != stat(title->path, &path_stat))
        return 0;

    return S_ISDIR(path_stat.st_mode);
}

static int func(const int fd, const request_t *const request, void *arg)
{
    if (0 > fd || NULL == request || NULL != arg)
    {
        LOG_M(ERROR, "Unexpected arguments in index handler");

        return EXIT_FAILURE;
    }

    const char *item = NULL;
    char *message = NULL;
    char *list = NULL;
    char *current = NULL;
    size_t asize = START_SIZE;
    size_t csize = START_SIZE;

    const request_title_t *title = request_title(request);

    if (!title)
    {
        LOG_M(ERROR, "Internel request_t error");

        return EXIT_FAILURE;
    }

    LOG_F(DEBUG, "Index request for directory: \"%s\"", title->path);

    int rc = EXIT_SUCCESS;
    const char *path = NULL;
    struct dirent *entry = NULL;
    DIR *dir = opendir(title->path);

    if (NULL == dir)
    {
        LOG_F(WARNING, "Request for unknown directory \"%s\"", title->path);
        rc = EXIT_FAILURE;
    }

    if (EXIT_SUCCESS == rc)
    {
        size_t len = strlen(title->path);
        path = title->path + len - 1;

        for (;title->path != path && '/' != *path; --path);

        ++path;
    }

    if (EXIT_SUCCESS == rc)
    {
        list = malloc(START_SIZE);
        current = list;

        if (NULL == list)
        {
            LOG_M(ERROR, "Allocation error");
            rc = EXIT_FAILURE;
        }
    }

    for (int len; EXIT_SUCCESS == rc && NULL != (entry = readdir(dir));)
    {
        if (DT_DIR & entry->d_type)
        {
            if (0 == *path)
                item = ITEMDR;
            else
                item = ITEMD;
        }
        else
        {
            if (0 == *path)
                item = ITEMR;
            else
                item = ITEM;
        }

        len = snprintf(NULL, 0, item, path, entry->d_name, entry->d_name);

        if (0 > len)
        {
            LOG_M(ERROR, "sprintf error");
            rc = EXIT_FAILURE;
        }

        if (EXIT_SUCCESS == rc && (size_t)len > csize)
        {
            size_t size = asize * 2;
            char *tmp = realloc(list, size);

            if (NULL == tmp)
            {
                LOG_M(ERROR, "Allocation error");
                rc = EXIT_FAILURE;
            }
            else
            {
                list = tmp;
                current = list + asize - csize;
                csize += asize;
                asize = size;
            }
        }

        if (EXIT_SUCCESS == rc)
        {
            if (0 > snprintf(current, csize, item, path, entry->d_name,
                             entry->d_name))
            {
                LOG_M(ERROR, "sprintf error");
                rc = EXIT_FAILURE;
            }
            else
            {
                current += len;
                csize -= len;
            }
        }
    }

    if (NULL != dir)
        closedir(dir);

    ssize_t len;

    if (EXIT_SUCCESS == rc)
    {
        len = snprintf(NULL, 0, FORM, title->path, list);

        if (0 > len)
        {
            LOG_M(ERROR, "sprintf error");
            rc = EXIT_FAILURE;
        }
    }

    if (EXIT_SUCCESS == rc)
    {
        message = malloc(len + 1);

        if (NULL == message)
        {
            LOG_M(ERROR, "Allocation error");
            rc = EXIT_FAILURE;
        }
    }

    if (EXIT_SUCCESS == rc && 0 > snprintf(message, len + 1, FORM, title->path, list))
    {
        LOG_M(ERROR, "sprintf error");
        rc = EXIT_FAILURE;
    }

    if (EXIT_SUCCESS == rc && len != send(fd, message, len, 0))
    {
        char buf[200];
        strerror_r(errno, buf, 200);
        LOG_F(ERROR, "send error: %s", buf);
        rc = EXIT_FAILURE;
    }

    free(list);
    free(message);

    return rc;
}

handler_t index_request_get(void)
{
    handler_t handler = {check, func, NULL, NULL};

    return handler;
}

