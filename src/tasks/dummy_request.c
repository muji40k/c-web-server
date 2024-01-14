#include "dummy_request.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

static int check(const request_t *const request)
{
    if (NULL == request)
        return 0;

    const request_title_t *title = request_title(request);

    if (NULL == title)
        return 0;

    return !strcmp(title->path, "/dummy");
}

static int func(const int fd, const request_t *const request, void *arg)
{
    if (0 > fd || NULL == request || NULL != arg)
        return EXIT_FAILURE;

    static const char *const message = "HTTP/1.1 200 OK\r\n\r\n";
    static ssize_t len = -1;

    if (-1 == len)
        len = strlen(message);

    int rc = EXIT_SUCCESS;

    if (len != send(fd, message, len, 0))
        rc = EXIT_FAILURE;

    return rc;
}

handler_t dummy_request_get(void)
{
    handler_t handler = {check, func, NULL, NULL};

    return handler;
}

