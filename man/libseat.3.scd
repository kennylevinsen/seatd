libseat(3)

# NAME

libseat - A seat management library

# SYNOPSIS

*\#include <libseat.h>*

# DESCRIPTION

*libseat* provides seat management with multiple backends in an easy to use
package.

See the respective sub-pages for more information.

# BACKENDS

*libseat* supports the following backends:
- *seatd(1)*
- (e)logind
- builtin, using an embedded *seatd(1)* instance

The backends are auto-detected at runtime, but can be forced with the
*LIBSEAT_BACKEND* environment variable.

# SEE ALSO

*seatd(1)*, *libseat-seat(3)*, *libseat-device(3)*, *libseat-session(3)*, *libseat-dispatch(3)*, *libseat-log(3)*

# AUTHORS

Maintained by Kenny Levinsen <contact@kl.wtf>. For more information about seatd
development, see https://sr.ht/~kennylevinsen/seatd.
