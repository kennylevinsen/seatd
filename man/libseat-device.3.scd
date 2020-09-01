libseat_device(3)

# NAME

libseat_open_device, libseat_close_device - libseat device management

# SYNOPSIS

*\#include <libseat.h>*

*int libseat_open_device(struct libseat \**_seat_*, const char \**_path_*, int \**_fd_*);*++
*int libseat_close_device(struct libseat \**_seat_*, int *_device_id_*);*

# DESCRIPTION

## libseat_open_device()

Opens a device on the seat, returning its device ID and placing the fd in
the specified pointer.

This will only succeed if the seat is active and the device is of a type
permitted for opening on the backend, such as drm and evdev.

The device may be revoked in some situations, such as in situations where a
seat session switch is being forced.

Returns the device id on success. Returns -1 and sets errno on error.

## libseat_close_device()

Closes a device that has been opened on the seat using the device_id from
libseat_open_device.

Returns 0 on success. Returns -1 and sets errno on error.

# SEE ALSO

*libseat(3)*, *libseat-seat(3)*

# AUTHORS

Maintained by Kenny Levinsen <contact@kl.wtf>. For more information about seatd
development, see https://sr.ht/~kennylevinsen/seatd.
