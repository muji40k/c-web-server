#ifndef _LOGGER_H_
#define _LOGGER_H_

#include <stdarg.h>

typedef enum
{
    ERROR,
    WARNING,
    INFO,
    DEBUG,
    ALL
} log_level_t;

typedef struct
{
    log_level_t limit;
    void (*function)(void *arg, log_level_t level, const char *const format,
                     va_list list);
    void (*post)(void *arg);
    void *arg;
} logger_t;

#define LOG_INIT(logger) _logger_set((logger))
#define LOG_CLOSE() _logger_close()
#define LOG_F(priority, format, ...) _logger_log((priority), "[%s] " format, __func__, __VA_ARGS__)
#define LOG_M(priority, msg) _logger_log((priority), "[%s] " msg, __func__)

void logger_log(logger_t logger, log_level_t level, const char *const format, ...);

void _logger_set(logger_t logger);
void _logger_close(void);
void _logger_log(log_level_t level, const char *const format, ...);

#endif

