libseat-session(3)

# NAME

libseat_switch_session - libseat session management

# SYNOPSIS

*\#include <libseat.h>*

*int libseat_switch_session(struct libseat \**_seat_*, int *_session_*);*

# DESCRIPTION

Requests that the seat switches session to the specified session number.
For seats that are VT-bound, the session number matches the VT number, and
switching session results in a VT switch.

A call to libseat_switch_session does not imply that a switch will occur,
and the caller should assume that the session continues unaffected.

Returns 0 on success. Returns -1 and sets errno on error.

# SEE ALSO

*libseat(3)*, *libseat-seat(3)*

# AUTHORS

Maintained by Kenny Levinsen <contact@kl.wtf>. For more information about seatd
development, see https://sr.ht/~kennylevinsen/seatd.
