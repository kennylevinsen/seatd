#ifndef _LOG_H
#define _LOG_H

#include <stdarg.h>

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

#define log_infof(fmt, ...) \
	_logf(LOGLEVEL_INFO, "[%s:%d] %s: " fmt, __FILENAME__, __LINE__, __func__, __VA_ARGS__)

#define log_info(str) _logf(LOGLEVEL_INFO, "[%s:%d] %s: %s", __FILENAME__, __LINE__, __func__, str)

#define log_errorf(fmt, ...) \
	_logf(LOGLEVEL_ERROR, "[%s:%d] %s: " fmt, __FILENAME__, __LINE__, __func__, __VA_ARGS__)

#define log_error(str) \
	_logf(LOGLEVEL_ERROR, "[%s:%d] %s: %s", __FILENAME__, __LINE__, __func__, str)

#ifdef DEBUG
#define log_debugf(fmt, ...) \
	_logf(LOGLEVEL_DEBUG, "[%s:%d] %s: " fmt, __FILENAME__, __LINE__, __func__, __VA_ARGS__)

#define log_debug(str) \
	_logf(LOGLEVEL_DEBUG, "[%s:%d] %s: %s", __FILENAME__, __LINE__, __func__, str)
#else
#define log_debugf(fmt, ...)
#define log_debug(str)
#endif

enum log_level {
	LOGLEVEL_SILENT = 0,
	LOGLEVEL_ERROR = 1,
	LOGLEVEL_INFO = 2,
	LOGLEVEL_DEBUG = 3,
	LOGLEVEL_LAST,
};

void log_init(enum log_level level);
void _logf(enum log_level level, const char *fmt, ...) ATTRIB_PRINTF(2, 3);

#endif
