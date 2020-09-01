libseat-dispatch(3)

# NAME

libseat_get_fd, libseat_dispatch - libseat event dispatch

# SYNOPSIS

*\#include <libseat.h>*

*int libseat_get_fd(struct libseat \**_seat_*);*++
*int libseat_dispatch(struct libseat \**_seat_*, int *_timeout_*);*

# DESCRIPTION

## libseat_get_fd()

Retrieve the pollable connection fd for a given libseat instance. Used to
poll the libseat connection for events that need to be dispatched.

Returns a pollable fd on success. Returns -1 and sets errno on error.

## libseat_dispatch()

Reads and dispatches events on the libseat connection fd.

The specified timeout dictates how long libseat might wait for data if none
is available: 0 means that no wait will occur, -1 means that libseat might
wait indefinitely for data to arrive, while > 0 is the maximum wait in
milliseconds that might occur.

Returns a positive number signifying processed internal messages on success.
Returns 0-if no messages were processed. Returns -1 and sets errno on error.

# SEE ALSO

*libseat(3)*, *libseat-seat(3)*

# AUTHORS

Maintained by Kenny Levinsen <contact@kl.wtf>. For more information about seatd
development, see https://sr.ht/~kennylevinsen/seatd.
