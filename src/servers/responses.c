#include "responses.h"

#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <string.h>

#define ERROR_MESSAGE                        \
"HTTP/1.1 %d %s\r\n"                         \
"Content-Type: text/html; charset=UTF-8\r\n" \
"\r\n"                                       \
"<html>"                                     \
    "<head>"                                 \
        "<title>Error occured</title>"       \
    "</head>"                                \
    "<body>"                                 \
        "<h1>Error</h1>"                     \
        "<p>%s</p>"                          \
    "</body>"                                \
"</html>"

int send_internal_error(int socket, http_response_code_t code, const char *message)
{
    const char *desc = http_response_code_description(code);

    ssize_t len = snprintf(NULL, 0, ERROR_MESSAGE, code, desc, message);
    char *buffer = malloc(len + 1);

    if (!buffer) {
        return EXIT_FAILURE;
    }

    int rc = EXIT_SUCCESS;
    snprintf(buffer, len + 1, ERROR_MESSAGE, code, desc, message);

    if (len != send(socket, buffer, len, 0)) {
        rc = EXIT_FAILURE;
    }

    free(buffer);
    return rc;
}

#define REFUSE_MESSAGE                                                  \
"HTTP/1.1 503 Service Unavailable\r\n"                                  \
"Content-Type: text/html; charset=UTF-8\r\n"                            \
"\r\n"                                                                  \
"<html>"                                                                \
    "<head>"                                                            \
        "<title>Resource Busy</title>"                                  \
    "</head>"                                                           \
    "<body>"                                                            \
        "<h1>Resource Busy</h1>"                                        \
        "<p>Your request cannot be completed at this time. Please try " \
           "again later.</p>"                                           \
    "</body>"                                                           \
"</html>"

int send_refuse_connection(int socket)
{
    static ssize_t len = -1;

    if (-1 == len) {
        len = strlen(REFUSE_MESSAGE);
    }

    int rc = EXIT_SUCCESS;

    if (len != send(socket, REFUSE_MESSAGE, len, 0)) {
        rc = EXIT_FAILURE;
    }

    return rc;
}

