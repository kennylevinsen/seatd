#ifndef _VISIBILITY_H
#define _VISIBILITY_H

#ifdef __GNUC__
#define ATTRIB_PRINTF(start, end) __attribute__((format(printf, start, end)))
#else
#define ATTRIB_PRINTF(start, end)
#endif

#define STRLEN(s) ((sizeof(s) / sizeof(s[0])) - 1)

#endif
