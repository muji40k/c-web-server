#define _POSIX_C_SOURCE 200112L
#define _GNU_SOURCE
#include "partial_file_request.h"

#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "logger.h"

#define WLOG_F(priority, format, ...) LOG_F((priority), "[%d] " format, gettid(), __VA_ARGS__)
#define WLOG_M(priority, msg) LOG_F((priority), "[%d] " msg, gettid())

// 100 MBs
#define BUFSIZE 104857600
// #define BUFSIZE (4 * 4096)
// #define BUFSIZE (2000)


static int check(const request_t *const request)
{
    if (NULL == request)
        return 0;

    const request_title_t *title = request_title(request);

    if (NULL == title
        || (strcmp("GET", title->method)
            && strcmp("HEAD", title->method)))
        return 0;

    return 1;
}

#define CRLF      "\r\n"
#define FORMAT    "HTTP/1.1 200 OK\r\nContent-Type: %s\r\n%s"
#define FRANGE    "Content-Range: bytes %zu-%zu/%zu\r\n"
#define NOT_FOUND "HTTP/1.1 404 Not Found\r\n\r\n"

static int send_not_found(const int fd)
{
    int rc = EXIT_SUCCESS;
    ssize_t len = strlen(NOT_FOUND);

    if (len != send(fd, NOT_FOUND, len, 0))
    {
        char buf[200];
        strerror_r(errno, buf, 200);
        WLOG_F(ERROR, "send error: %s", buf);
        rc = EXIT_FAILURE;
    }

    return rc;
}

static int send_file(const int socket, const int file, const file_type_t *type,
                     const int head)
{
    int rc = EXIT_SUCCESS;
    char *buffer = malloc(BUFSIZE);

    if (NULL == buffer)
    {
        WLOG_M(ERROR, "Allocation error");

        return EXIT_FAILURE;
    }

    int hlen = sprintf(buffer, FORMAT CRLF, type->mime, type->addition);
    struct stat stat;

    if (0 > hlen)
    {
        WLOG_M(ERROR, "sprintf error");
        rc = EXIT_FAILURE;
    }

    if (EXIT_SUCCESS == rc && -1 == fstat(file, &stat))
    {
        WLOG_M(ERROR, "sprintf error");
        rc = EXIT_FAILURE;
    }

    if (EXIT_SUCCESS == rc && head)
    {
        if (hlen != send(socket, buffer, hlen, 0))
        {
            char buf[200];
            strerror_r(errno, buf, 200);
            WLOG_F(ERROR, "send error: %s", buf);
            rc = EXIT_FAILURE;
        }
    }
    else if (EXIT_SUCCESS == rc)
    {
        ssize_t offset = hlen;
        ssize_t step = BUFSIZE - hlen;
        char *bbuf = buffer + hlen;

        for (size_t start = 0, end = step;
             EXIT_SUCCESS == rc && (size_t)stat.st_size > start;
             start = end)
        {
            end = start + step;

            if (end > (size_t)stat.st_size)
                end = stat.st_size;

            ssize_t rd = read(file, bbuf, step);

            if (-1 == rd)
            {
                char buf[200];
                strerror_r(errno, buf, 200);
                WLOG_F(ERROR, "read error: %s", buf);
                rc = EXIT_FAILURE;
            }

            ssize_t total = offset + rd;

            if (EXIT_SUCCESS == rc
                && total != send(socket, buffer, total, 0))
            {
                char buf[200];
                strerror_r(errno, buf, 200);
                WLOG_F(ERROR, "send error: %s", buf);
                rc = EXIT_FAILURE;
            }

            if (offset)
            {
                bbuf = buffer;
                step = BUFSIZE;
                offset = 0;
            }
        }
    }

    free(buffer);

    return rc;
}

static int func(const int fd, const request_t *const request, void *arg)
{
    if (0 > fd || NULL == request || NULL == arg)
    {
        WLOG_M(ERROR, "Unexpected arguments in file handler");

        return EXIT_FAILURE;
    }

    const request_title_t *title = request_title(request);

    if (!title)
    {
        WLOG_M(ERROR, "Internel request_t error");

        return EXIT_FAILURE;
    }

    int head = 0;

    if (!strcmp(title->method, "HEAD"))
        head = 1;

    WLOG_F(DEBUG, "File request for file: \"%s\"", title->path);

    const file_type_t *type = file_type_bank_get(arg, title->path);

    int file = open(title->path, O_RDONLY);
    int rc = EXIT_SUCCESS;

    if (-1 == file && ENOENT == errno)
    {
        WLOG_F(WARNING, "Request for unknown file \"%s\"", title->path);
        rc = send_not_found(fd);
    }
    else if (-1 != file)
    {
        rc = send_file(fd, file, type, head);
        close(file);
    }
    else
    {
        char buf[200];
        strerror_r(errno, buf, 200);
        WLOG_F(ERROR, "open error: %s", buf);
        rc = EXIT_FAILURE;
    }

    return rc;
}

static void free_wrap(void **const arg)
{
    file_type_bank_free((file_type_bank_t **)arg);
}

handler_t partial_file_request_get(file_type_bank_t *const bank)
{
    handler_t handler = {check, func, bank, free_wrap};

    return handler;
}

