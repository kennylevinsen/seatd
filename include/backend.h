#ifndef _SEATD_BACKEND_H
#define _SEATD_BACKEND_H

#include "libseat.h"

struct libseat_impl;
struct libseat_seat_listener;

struct libseat {
	const struct libseat_impl *impl;
};

struct named_backend {
	const char *name;
	const struct libseat_impl *backend;
};

struct libseat_impl {
	struct libseat *(*open_seat)(struct libseat_seat_listener *listener, void *data);
	int (*disable_seat)(struct libseat *seat);
	int (*close_seat)(struct libseat *seat);
	const char *(*seat_name)(struct libseat *seat);

	int (*open_device)(struct libseat *seat, const char *path, int *fd);
	int (*close_device)(struct libseat *seat, int device_id);
	int (*switch_session)(struct libseat *seat, int session);

	int (*get_fd)(struct libseat *seat);
	int (*dispatch)(struct libseat *seat, int timeout);
};

#endif
