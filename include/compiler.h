#ifndef _VISIBILITY_H
#define _VISIBILITY_H

#ifdef __GNUC__
#define ATTRIB_PRINTF(start, end) __attribute__((format(printf, start, end)))
#define LIBSEAT_EXPORT		  __attribute__((visibility("default")))
#else
#define ATTRIB_PRINTF(start, end)
#define LIBSEAT_EXPORT
#endif

#define STRLEN(s) ((sizeof(s) / sizeof(s[0])) - 1)

#endif
