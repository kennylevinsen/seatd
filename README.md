# seatd (tentative)

Seat management daemon. Work in progress, name still up for debate.

## Goals for seatd

1. Provide a simple daemon that manages the user seat - nothing more, nothing less.
2. Support all sorts of setups - with and without systemd, BSD, ...
3. Provide a seat management library that can use seatd, (e)logind or direct file access to remove complexity from shell implementations.

## Non-goals for seatd

1. Backlight control
2. Dock detection
3. Power button handling
4. ... Anything that is not related to mediating access to seat devices.

## Why ...

### ... seatd, the daemon?

There is no good implementation of modern seat management. We're well past the time of running our user shells as root/suid to grab input devices, but there is little in the way of alternatives.

There is logind of course, but that only works for systemd-based Linux distributions (see "What about elogind?"), and "simple" is definitely not within the set of adjectives that can be used to describe logind. For those in the dark, [take a glance at its API](https://www.freedesktop.org/wiki/Software/systemd/logind/).

ConsoleKit2 unfortunately appears abandoned.

####  What about elogind?

systemd is not written with isolated modules in mind - that is, logind and others have internal hard dependencies on other systemd components, such as "PID1" and journald - and fighting against core upstream design decisions is a lot of work, which repeats every time one syncs with upstream.

Why spend all that work isolating logind and keeping up with upstream when *we can do better*?

### ... seatd, the library?

[In order to not be part of the problem](https://xkcd.com/927/). If seatd ended up being yet another session backend to implement, it wouldn't really help much, as user shells like [sway](https://github.com/swaywm/sway) would still have to handle logind, and possibly direct session management.

Instead, to make the lives of these shells easier, a simple library would be the only thing they need to interact with. Things like direct session management (for when neither seatd nor logind is available) could also be kept in, and maintained, in one place rather than having duplicated code.

## seatd API (tentative)

seatd will operate over unix sockets.

### Methods

- open_seat(seat_listener) -> seat_handle
- seat_open_device(seat_handle, device_handle) -> file_handle
- seat_close_device(seat_handle, file_handle) -> file_handle
- seat_ack_disable(seat_handle)
- close_seat(seat_handle)

### Events

- seat_disable: The seat is being disabled. The seat holder must suspend all seat usage and ack the disable when done.
- seat_resume: The seat is being enabled. The seat holder is free to resume all seat usage.
- seat_device_added(device_handle): A new device has been added to the seat.
- seat_device_removed(device_handle): A device has been removed from the seat.

## Backlight control, button monitoring, ...

These features do not belong in seatd. However, side-projects might be created to handle this.

Power button/lid switch/... management can be done in a separate daemon with no interaction with seatd whatsoever. As there is no reason for it to reside in seatd, it *must not* reside in seatd.

The same goes for backlight control. Now, one might wish to restrict backlight access to the currently enabled seat, which might require some thought. This will preferably still be done with no dependency on setad.

Regardless of what is done, simplicity of seatd is top priority, and a significant complexity increase just to permit e.g. restricted backlight control will not be accepted.

## Where's the code?

The rough design should be hashed out first, before code is written that ends up cementing the design in place.

## I want more

Go to #kennylevinsen @ chat.freenode.net to discuss.
