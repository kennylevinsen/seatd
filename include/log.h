#ifndef _LOG_H
#define _LOG_H

#include <stdarg.h>

#include "libseat.h"

#ifdef __GNUC__
#define ATTRIB_PRINTF(start, end) __attribute__((format(printf, start, end)))
#else
#define ATTRIB_PRINTF(start, end)
#endif

#ifdef REL_SRC_DIR
#define __FILENAME__ ((const char *)__FILE__ + sizeof(REL_SRC_DIR) - 1)
#else
#define __FILENAME__ __FILE__
#endif

#define log_infof(fmt, ...)                                                                 \
	_logf(LIBSEAT_LOG_LEVEL_INFO, "[%s:%d] %s: " fmt, __FILENAME__, __LINE__, __func__, \
	      __VA_ARGS__)

#define log_info(str) \
	_logf(LIBSEAT_LOG_LEVEL_INFO, "[%s:%d] %s: %s", __FILENAME__, __LINE__, __func__, str)

#define log_errorf(fmt, ...)                                                                 \
	_logf(LIBSEAT_LOG_LEVEL_ERROR, "[%s:%d] %s: " fmt, __FILENAME__, __LINE__, __func__, \
	      __VA_ARGS__)

#define log_error(str) \
	_logf(LIBSEAT_LOG_LEVEL_ERROR, "[%s:%d] %s: %s", __FILENAME__, __LINE__, __func__, str)

#ifdef DEBUG
#define log_debugf(fmt, ...)                                                                 \
	_logf(LIBSEAT_LOG_LEVEL_DEBUG, "[%s:%d] %s: " fmt, __FILENAME__, __LINE__, __func__, \
	      __VA_ARGS__)

#define log_debug(str) \
	_logf(LIBSEAT_LOG_LEVEL_DEBUG, "[%s:%d] %s: %s", __FILENAME__, __LINE__, __func__, str)
#else
#define log_debugf(fmt, ...)
#define log_debug(str)
#endif

void log_init(enum libseat_log_level level);
void _logf(enum libseat_log_level level, const char *fmt, ...) ATTRIB_PRINTF(2, 3);

#endif
