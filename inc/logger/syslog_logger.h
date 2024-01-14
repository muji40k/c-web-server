#ifndef _SYSLOG_LOGGER_H_
#define _SYSLOG_LOGGER_H_

#include <syslog.h>

#include "logger.h"

#define SYSLOG_LOGGER() LOG_INIT(syslog_logger_init(ALL))
#define SYSLOG_LOGGER_L(level) LOG_INIT(syslog_logger_init(level))

logger_t syslog_logger_init(log_level_t level);

#endif

