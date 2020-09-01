libseat_seat(3)

# NAME

libseat_open_seat, libseat_disable_seat, libseat_close_seat - libseat seat management

# SYNOPSIS

*\#include <libseat.h>*

*struct libseat;*

*struct libseat_seat_listener {*++
*	void (\**_enable_seat_*)(struct libseat \**_seat_*, void \**_userdata_*);*++
*	void (\**_disable_seat_*)(struct libseat \**_seat_*, void \**_userdata_*);*++
*};*

*struct libseat \*libseat_open_seat(struct libseat_seat_listener \**_listener_*, void \**_userdata_*);*++
*int libseat_disable_seat(struct libseat \**_seat_*);*++
*int libseat_close_seat(struct libseat \**_seat_*);*

*const char \*libseat_seat_name(struct libseat \**_seat_*);*

# DESCRIPTION

The functions described here manage a seat, backed by one of the backends
supported by libseat.

## libseat_open_seat()

Opens a seat, taking control of it if possible and returning a pointer to the
libseat instance. If LIBSEAT_BACKEND is set, the specified backend is used.
Otherwise, the first successful backend will be used.

The seat listener specified is used to signal events on the seat, and must be
non-NULL. The userdata pointer will be provided in all calls to the seat
listener.

The available backends, if enabled at compile-time, are: seatd, logind and
builtin.

To use builtin, the process must have CAP_SYS_ADMIN or be root at the time of
the call. These privileges can be dropped at any point after the call.

The returned pointer must be destroyed with libseat_close_seat.

Returns a pointer to an opaque libseat struct on success. Returns NULL and sets
errno on error.

## libseat_disable_seat()

Disables a seat, used in response to a disable_seat event. After disabling
the seat, the seat devices must not be used until enable_seat is received,
and all requests on the seat will fail during this period.

Returns 0 on success. -1 and sets errno on error.

## libseat_close_seat()

Closes the seat. This frees the libseat structure.

Returns 0 on success. Returns -1 and sets errno on error.

## libseat_seat_name()

Retrieves the name of the seat that is currently made available through the
provided libseat instance.

The returned string is owned by the libseat instance, and must not be
modified. It remains valid as long as the seat is open.

# SEE ALSO

*libseat(3)*, *libseat-device(3)*, *libseat-session(3)*, *libseat-dispatch(3)*, *libseat-log(3)*

# AUTHORS

Maintained by Kenny Levinsen <contact@kl.wtf>. For more information about seatd
development, see https://sr.ht/~kennylevinsen/seatd.
