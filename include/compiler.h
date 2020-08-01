#ifndef _VISIBILITY_H
#define _VISIBILITY_H

#ifdef __GNUC__
#define ATTRIB_PRINTF(start, end) __attribute__((format(printf, start, end)))
#else
#define ATTRIB_PRINTF(start, end)
#endif

#if defined(__GNUC__) && __GNUC__ >= 4
#define LIBSEAT_EXPORT __attribute__((visibility("default")))
#define ALWAYS_INLINE  __attribute__((always_inline)) inline
#else
#define LIBSEAT_EXPORT
#define ALWAYS_INLINE inline
#endif

#define STRLEN(s) ((sizeof(s) / sizeof(s[0])) - 1)

#endif
