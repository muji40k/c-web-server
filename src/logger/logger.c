#include "logger.h"

#include <stddef.h>

static logger_t _logger = {ALL, NULL, NULL, NULL};

void logger_log(logger_t logger, log_level_t level, const char *const format, ...)
{
    if (NULL == logger.function)
        return;

    if (logger.limit <= level)
        return;

    va_list list;
    va_start(list, format);

    logger.function(logger.arg, level, format, list);
}

void _logger_set(logger_t logger)
{
    if (logger.function)
        _logger = logger;
}

void _logger_close(void)
{
    if (_logger.post)
        _logger.post(_logger.arg);
}

void _logger_log(log_level_t level, const char *const format, ...)
{
    if (NULL == _logger.function)
        return;

    if (_logger.limit <= level)
        return;

    va_list list;
    va_start(list, format);

    _logger.function(_logger.arg, level, format, list);
}

