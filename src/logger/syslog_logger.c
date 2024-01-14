#define _DEFAULT_SOURCE
#include "syslog_logger.h"

#include <stddef.h>

int priority_map(log_level_t level)
{
    switch (level)
    {
        case (ERROR):
            return LOG_ERR;
        case (WARNING):
            return LOG_WARNING;
        case (INFO):
            return LOG_INFO;
        case (DEBUG):
            return LOG_DEBUG;
        case (ALL):
            return LOG_DEBUG;
    }

    return LOG_DEBUG;
}

void function(void *arg, log_level_t level, const char *const format,
              va_list list)
{
    if (NULL != arg)
        return;

    vsyslog(priority_map(level), format, list);
}

void post(void *arg)
{
    if (NULL != arg)
        return;

    closelog();
}

logger_t syslog_logger_init(log_level_t level)
{
    openlog(NULL, LOG_NDELAY, 0);
    logger_t out = {level, function, post, NULL};

    return out;
}

