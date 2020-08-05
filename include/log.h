#ifndef _LOG_H
#define _LOG_H

#include <stdarg.h>

#ifdef __GNUC__
#define ATTRIB_PRINTF(start, end) __attribute__((format(printf, start, end)))
#else
#define ATTRIB_PRINTF(start, end)
#endif

#ifdef LIBSEAT_REL_SRC_DIR
#define _LIBSEAT_FILENAME ((const char *)__FILE__ + sizeof(LIBSEAT_REL_SRC_DIR) - 1)
#else
#define _LIBSEAT_FILENAME __FILE__
#endif

#define log_infof(fmt, ...)                                                                    \
	_libseat_logf(LIBSEAT_INFO, "[%s:%d] %s: " fmt, _LIBSEAT_FILENAME, __LINE__, __func__, \
		      __VA_ARGS__)

#define log_info(str) \
	_libseat_logf(LIBSEAT_INFO, "[%s:%d] %s: %s", _LIBSEAT_FILENAME, __LINE__, __func__, str)

#define log_errorf(fmt, ...)                                                                    \
	_libseat_logf(LIBSEAT_ERROR, "[%s:%d] %s: " fmt, _LIBSEAT_FILENAME, __LINE__, __func__, \
		      __VA_ARGS__)

#define log_error(str) \
	_libseat_logf(LIBSEAT_ERROR, "[%s:%d] %s: %s", _LIBSEAT_FILENAME, __LINE__, __func__, str)

#ifdef DEBUG
#define log_debugf(fmt, ...)                                                                    \
	_libseat_logf(LIBSEAT_DEBUG, "[%s:%d] %s: " fmt, _LIBSEAT_FILENAME, __LINE__, __func__, \
		      __VA_ARGS__)

#define log_debug(str) \
	_libseat_logf(LIBSEAT_DEBUG, "[%s:%d] %s: %s", _LIBSEAT_FILENAME, __LINE__, __func__, str)
#else
#define log_debugf(fmt, ...)
#define log_debug(str)
#endif

enum libseat_log_level {
	LIBSEAT_SILENT = 0,
	LIBSEAT_ERROR = 1,
	LIBSEAT_INFO = 2,
	LIBSEAT_DEBUG = 3,
	LIBSEAT_LOG_LEVEL_LAST,
};

void libseat_log_init(enum libseat_log_level level);
void _libseat_logf(enum libseat_log_level level, const char *fmt, ...) ATTRIB_PRINTF(2, 3);

#endif
