#define _POSIX_C_SOURCE 200112L
#include "file_request.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <errno.h>

#include "logger.h"

file_type_bank_t *file_type_bank_init(void)
{
    return list_init(sizeof(file_type_t));
}

int file_type_bank_add(file_type_bank_t *const bank, file_type_t *type)
{
    return list_push_back(bank, type);
}

static const char *get_ext(const char *const path)
{
    size_t len = strlen(path);

    if (0 == len)
        return path;

    const char *ext = path + len - 1;

    for (int run = 1; path != ext && run;)
    {
        if ('/' == *ext || '.' == *ext)
            run = 0;
        else
            ext--;
    }

    if ('.' != *ext)
        return NULL;

    return ext + 1;
}

static int find_by_ext(const void *const arg, const void *const value)
{
    if (NULL == arg || NULL == value)
        return 0;

    return !strcasecmp(arg, ((file_type_t *)value)->ext);
}

static const file_type_t empty =
{
    .ext = "",
    .mime = "application/octet-stream",
    .addition = "Content-Disposition: attachment\r\n"
};

const file_type_t *file_type_bank_get(file_type_bank_t *const bank,
                                      const char *const path)
{
    if (NULL == bank || NULL == path)
        return NULL;

    const char *ext = get_ext(path);

    if (NULL == ext)
        return &empty;

    file_type_t *out = NULL;
    list_filter_t filter = {find_by_ext, ext};

    if (EXIT_SUCCESS != list_find(bank, &filter, (void **)&out))
        return &empty;

    if (NULL == out)
        return &empty;

    return out;
}

void file_type_bank_free(file_type_bank_t **const bank)
{
    list_free(bank);
}

static int check(const request_t *const request)
{
    if (NULL == request)
        return 0;

    const request_title_t *title = request_title(request);

    if (NULL == title || strcmp("GET", title->method))
        return 0;

    return 1;
}

static int func(const int fd, const request_t *const request, void *arg)
{
    if (0 > fd || NULL == request || NULL == arg)
    {
        LOG_M(ERROR, "Unexpected arguments in file handler");

        return EXIT_FAILURE;
    }

    char *message = NULL;
    int is_free = 0;
    static char *const not_found = "HTTP/1.1 404 Not Found\r\n\r\n";
    static const char *const format = "HTTP/1.1 200 OK\r\nContent-Type: %s\r\n%s\r\n";
    ssize_t len;

    const request_title_t *title = request_title(request);

    if (!title)
    {
        LOG_M(ERROR, "Internel request_t error");

        return EXIT_FAILURE;
    }

    LOG_F(DEBUG, "File request for file: \"%s\"", title->path);

    const file_type_t *type = file_type_bank_get(arg, title->path);

    FILE *file = fopen(title->path, "rb");
    int rc = EXIT_SUCCESS;

    if (!file)
    {
        LOG_F(WARNING, "Request for unknown file \"%s\"", title->path);
        message = not_found;
        len = strlen(message);
    }
    else
    {
        int hlen = snprintf(NULL, 0, format, type->mime, type->addition);
        long blen = 0;

        if (0 > hlen)
        {
            LOG_M(ERROR, "sprintf error");
            rc = EXIT_FAILURE;
        }

        if (EXIT_SUCCESS == rc && -1 == fseek(file, 0, SEEK_END))
        {
            LOG_M(ERROR, "fseek error");
            rc = EXIT_FAILURE;
        }

        if (EXIT_SUCCESS == rc)
        {
            blen = ftell(file);

            if (-1 == blen)
            {
                LOG_M(ERROR, "ftell error");
                rc = EXIT_FAILURE;
            }
        }

        if (EXIT_SUCCESS == rc && -1 == fseek(file, 0, SEEK_SET))
        {
            LOG_M(ERROR, "fseek error");
            rc = EXIT_FAILURE;
        }

        if (EXIT_SUCCESS == rc)
        {
            message = malloc(hlen + blen);

            if (!message)
            {
                LOG_M(ERROR, "Unable to allocate");
                rc = EXIT_FAILURE;
            }
        }

        if (EXIT_SUCCESS == rc)
        {
            is_free = 1;
            len = hlen + blen;

            if (0 > sprintf(message, format, type->mime, type->addition))
            {
                LOG_M(ERROR, "sprintf error");
                rc = EXIT_FAILURE;
            }
        }

        if (EXIT_SUCCESS == rc
            && (unsigned long)blen != fread(message + hlen, sizeof(char),
                                            blen, file))
        {
            LOG_M(ERROR, "fread error");
            rc = EXIT_FAILURE;
        }

        fclose(file);
    }

    if (EXIT_SUCCESS == rc)
        if (len != send(fd, message, len, 0))
        {
            char buf[200];
            strerror_r(errno, buf, 200);
            LOG_F(ERROR, "send error: %s", buf);
            rc = EXIT_FAILURE;
        }

    if (is_free)
        free(message);

    return rc;
}

static void free_wrap(void **const arg)
{
    file_type_bank_free((file_type_bank_t **)arg);
}

handler_t file_request_get(file_type_bank_t *const bank)
{
    handler_t handler = {check, func, bank, free_wrap};

    return handler;
}

