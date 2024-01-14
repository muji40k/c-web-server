#include "print_request.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

static int check(const request_t *const request)
{
    if (NULL == request)
        return 0;

    return 1;
}

static int func(const int fd, const request_t *const request, void *arg)
{
    if (0 > fd || NULL == request || NULL != arg)
        return EXIT_FAILURE;

    static const char *const message = "HTTP/1.1 200 OK\r\n\r\n";
    static ssize_t len = -1;

    if (-1 == len)
        len = strlen(message);

    const request_title_t *title = request_title(request);
    const char *body = request_body(request);

    printf("%s %s %s\n", title->method, title->path, title->version);

    if (0 != *body)
        printf("BODY:\n%s\n", body);

    int rc = EXIT_SUCCESS;

    if (len != send(fd, message, len, 0))
        rc = EXIT_FAILURE;

    return rc;
}

handler_t print_request_get(void)
{
    handler_t handler = {check, func, NULL, NULL};

    return handler;
}

