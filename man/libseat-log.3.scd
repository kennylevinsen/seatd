libseat(3)

# NAME

libseat_set_log_handler, libseat_set_log_level, libseat_log_level, libseat_log_func - libseat log handling

# SYNOPSIS

*\#include <libseat.h>*

*enum libseat_log_level {*++
*	LIBSEAT_LOG_LEVEL_SILENT = 0,*++
*	LIBSEAT_LOG_LEVEL_ERROR = 1,*++
*	LIBSEAT_LOG_LEVEL_INFO = 2,*++
*	LIBSEAT_LOG_LEVEL_DEBUG = 3,*++
*	LIBSEAT_LOG_LEVEL_LAST,*++
*};*

*typedef void (\*libseat_log_func)(enum libseat_log_level *_level_*, const char \**_format_*, va_list *_args_*);*

*void libseat_set_log_handler(libseat_log_func *_handler_*);*++
*void libseat_set_log_level(enum libseat_log_level *_level_*);*

# DESCRIPTION

## libseat_set_log_handler()

Sets the handler for log messages.

The handler will be called for each message whose level is lower or equal
to the current log level. If the handler is NULL, the handler is reset to
the default.

## libseat_set_log_level()

Sets the libseat log level.

Only log messages whose level is lower or equal than the current log level
will be processed, others will be ignored.

# SEE ALSO

*libseat(3)*, *libseat-seat(3)*

# AUTHORS

Maintained by Kenny Levinsen <contact@kl.wtf>. For more information about seatd
development, see https://sr.ht/~kennylevinsen/seatd.
