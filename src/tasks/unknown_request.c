#define _POSIX_C_SOURCE 200112L
#include "unknown_request.h"

#include <stdio.h>
#include <string.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include "logger.h"

static int check(const request_t *const request)
{
    if (!request)
        return 0;

    return 1;
}

static int func(const int fd, const request_t *const request, void *arg)
{
    if (0 > fd || NULL == request || NULL != arg)
    {
        LOG_M(ERROR, "Unexpected arguments in \"unknown\" handler");

        return EXIT_FAILURE;
    }

    const request_title_t *title = request_title(request);

    if (NULL == title)
        LOG_M(ERROR, "Internel request_t error");
    else
        LOG_F(WARNING, "Unimplemented request: %s %s %s", title->method,
              title->path, title->version);

    static const char *const message = "HTTP/1.1 405 Method Not Allowed\r\n\r\n";
    static ssize_t len = -1;

    if (-1 == len)
        len = strlen(message);

    int rc = EXIT_SUCCESS;

    if (len != send(fd, message, len, 0))
    {
        char buf[200];
        strerror_r(errno, buf, 200);
        LOG_F(ERROR, "send error: %s", buf);
        rc = EXIT_FAILURE;
    }

    return rc;
}

handler_t unknown_request_get(void)
{
    handler_t handler = {check, func, NULL, NULL};

    return handler;
}

