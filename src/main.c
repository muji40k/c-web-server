#define _POSIX_C_SOURCE 200112L
#define _DEFAULT_SOURCE

#include <signal.h>
#include <string.h>

#include "syslog_logger.h"

#include "server.h"
#include "handler.h"

#include "index_request.h"
#include "partial_file_request.h"
#include "unknown_request.h"

#include <unistd.h>

struct args
{
    int valid;
    const char *cwd;
    int port;
    size_t threads;
    log_level_t level;
};

typedef struct
{
    int check;
    int rc;
} arg_res_t;

typedef arg_res_t (*arg_parser_t)(struct args *args, char ***arg, char **end);

arg_res_t args_thread(struct args *args, char ***arg, char **end)
{
    arg_res_t res = {0, EXIT_SUCCESS};

    if (strcmp("-n", **arg))
        return res;

    res.check = 1;

    if (end == ++(*arg))
        res.rc = EXIT_FAILURE;
    else
    {
        char *tmp = NULL;
        size_t threads = strtoull(**arg, &tmp, 10);

        if (0 != *tmp)
            res.rc = EXIT_FAILURE;
        else
        {
            args->threads = threads;
            ++(*arg);
        }
    }

    return res;
}

arg_res_t args_port(struct args *args, char ***arg, char **end)
{
    arg_res_t res = {0, EXIT_SUCCESS};

    if (strcmp("-p", **arg))
        return res;

    res.check = 1;

    if (end == ++(*arg))
        res.rc = EXIT_FAILURE;
    else
    {
        char *tmp = NULL;
        int port = strtol(**arg, &tmp, 10);

        if (0 != *tmp)
            res.rc = EXIT_FAILURE;
        else
        {
            args->port = port;
            ++(*arg);
        }
    }

    return res;
}

arg_res_t args_cwd(struct args *args, char ***arg, char **end)
{
    arg_res_t res = {0, EXIT_SUCCESS};

    if (NULL == end)
        return res;

    if ('-' == ***arg)
        return res;

    res.check = 1;
    args->cwd = *((*arg)++);

    return res;
}

arg_res_t args_log_level(struct args *args, char ***arg, char **end)
{
    arg_res_t res = {0, EXIT_SUCCESS};

    if (strcmp("-l", **arg))
        return res;

    res.check = 1;

    if (end == ++(*arg))
        res.rc = EXIT_FAILURE;
    else
    {
        if (!strcmp(**arg, "error"))
            args->level = ERROR;
        else if (!strcmp(**arg, "warning"))
            args->level = WARNING;
        else if (!strcmp(**arg, "info"))
            args->level = INFO;
        else if (!strcmp(**arg, "debug"))
            args->level = DEBUG;
        else if (!strcmp(**arg, "all"))
            args->level = ALL;
        else
            res.rc = EXIT_FAILURE;

        ++(*arg);
    }

    return res;
}


static const arg_parser_t parsers[] =
{
    args_thread, args_port, args_cwd, args_log_level
};

static const size_t psize = sizeof(parsers) / sizeof(parsers[0]);

struct args parse_args(int argc, char **argv)
{
    struct args args = {1, ".", 80, 10, INFO};
    argc--, argv++;

    for (char **end = argv + argc; args.valid && argv != end;)
    {
        arg_res_t res = {0, 0};

        for (size_t i = 0; !res.check && psize > i; i++)
            res = parsers[i](&args, &argv, end);

        if (!res.check || EXIT_SUCCESS != res.rc)
            args.valid = 0;
    }

    return args;
}

int set_server_root(const struct args *const args)
{
    if (-1 == chdir(args->cwd))
    {
        LOG_F(ERROR, "Unable to navigate to server directory \"%s\"", args->cwd);

        return EXIT_FAILURE;
    }

    if (-1 == chroot("."))
    {
        LOG_M(ERROR, "Unable to chroot");

        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

server_t *setup_server(const struct args *const args)
{
    struct sigaction new_action;
    new_action.sa_handler = server_termination_handler;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;

    if (-1 == sigaction(SIGINT, &new_action, NULL))
    {
        LOG_M(ERROR, "Unable to set sigaction");

        return NULL;
    }

    int rc = server_setup();
    server_t *server = NULL;

    if (EXIT_SUCCESS == rc)
    {
        server = server_init(args->port, args->threads);

        if (NULL == server)
        {
            LOG_M(ERROR, "Unable to init server");
            rc = errno;
        }
    }

    handler_t handler;

    if (EXIT_SUCCESS == rc)
    {
        handler = index_request_get();
        rc = server_register_handler(server, &handler);
    }

    if (EXIT_SUCCESS == rc)
    {
        file_type_bank_t *bank = file_type_bank_init();

        if (NULL == bank)
            rc = EXIT_FAILURE;
        else
        {
            file_type_t type = {"", "", ""};

            type.ext = "png";
            type.mime = "image/png";
            file_type_bank_add(bank, &type);
            type.ext = "jpg";
            type.mime = "image/jpg";
            file_type_bank_add(bank, &type);
            type.ext = "jpeg";
            type.mime = "image/jpeg";
            file_type_bank_add(bank, &type);
            type.ext = "gif";
            type.mime = "image/gif";
            file_type_bank_add(bank, &type);
            type.ext = "txt";
            type.mime = "text";
            file_type_bank_add(bank, &type);
            type.ext = "c";
            file_type_bank_add(bank, &type);
            type.ext = "html";
            type.mime = "text/html; charset=UTF-8";
            file_type_bank_add(bank, &type);
            type.ext = "css";
            type.mime = "text/css";
            file_type_bank_add(bank, &type);
            type.ext = "pdf";
            type.mime = "application/pdf";
            file_type_bank_add(bank, &type);
            type.ext = "mp4";
            type.mime = "video/mp4";
            file_type_bank_add(bank, &type);
            type.ext = "swf";
            type.mime = "video/vnd.sealed.swf";
            file_type_bank_add(bank, &type);

            handler = partial_file_request_get(bank);
            rc = server_register_handler(server, &handler);
        }
    }

    if (EXIT_SUCCESS == rc)
    {
        handler = unknown_request_get();
        rc = server_register_handler(server, &handler);
    }

    if (EXIT_SUCCESS == rc)
        LOG_M(INFO, "Server setup correct");

    return server;
}

int main(int argc, char **argv)
{
    struct args args = parse_args(argc, argv);

    if (!args.valid)
        return EXIT_FAILURE;

    SYSLOG_LOGGER_L(args.level);

    if (EXIT_SUCCESS != set_server_root(&args))
        return EXIT_FAILURE;

    server_t *server = setup_server(&args);

    if (NULL == server)
        return EXIT_FAILURE;

    int rc = server_mainloop(server);
    server_free(&server);
    server_destroy();

    LOG_CLOSE();

    return rc;
}

