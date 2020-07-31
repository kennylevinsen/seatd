#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "client.h"
#include "drm.h"
#include "evdev.h"
#include "list.h"
#include "log.h"
#include "protocol.h"
#include "seat.h"
#include "terminal.h"

struct seat *seat_create(const char *seat_name, bool vt_bound) {
	struct seat *seat = calloc(1, sizeof(struct seat));
	if (seat == NULL) {
		return NULL;
	}
	list_init(&seat->clients);
	seat->vt_bound = vt_bound;

	seat->seat_name = strdup(seat_name);
	if (seat->seat_name == NULL) {
		free(seat);
		return NULL;
	}

	log_debugf("created seat '%s' (vt_bound: %d)", seat_name, vt_bound);
	return seat;
}

void seat_destroy(struct seat *seat) {
	assert(seat);
	while (seat->clients.length > 0) {
		struct client *client = seat->clients.items[seat->clients.length - 1];
		// This will cause the client to remove itself from the seat
		assert(client->seat);
		client_kill(client);
	}

	free(seat->seat_name);
	free(seat);
}

int seat_add_client(struct seat *seat, struct client *client) {
	assert(seat);
	assert(client);

	if (client->seat != NULL) {
		log_error("cannot add client: client is already a member of a seat");
		return -1;
	}

	if (seat->vt_bound && seat->active_client != NULL) {
		log_error("cannot add client: seat is vt_bound and an active client already exists");
		return -1;
	}

	client->seat = seat;

	list_add(&seat->clients, client);
	log_debug("added client");
	return 0;
}

int seat_remove_client(struct seat *seat, struct client *client) {
	assert(seat);
	assert(client);
	assert(client->seat == seat);

	// We must first remove the client to avoid reactivation
	bool found = false;
	for (size_t idx = 0; idx < seat->clients.length; idx++) {
		struct client *c = seat->clients.items[idx];
		if (client == c) {
			list_del(&seat->clients, idx);
			found = true;
			break;
		}
	}

	if (!found) {
		log_debug("client was not on the client list");
	}

	if (seat->next_client == client) {
		seat->next_client = NULL;
	}

	while (client->devices.length > 0) {
		struct seat_device *device = client->devices.items[client->devices.length - 1];
		seat_close_device(client, device);
	}

	if (seat->active_client == client) {
		seat_close_client(seat, client);
	}

	client->seat = NULL;
	log_debug("removed client");

	return found ? 0 : -1;
}

struct seat_device *seat_find_device(struct client *client, int device_id) {
	assert(client);
	assert(client->seat);
	assert(device_id != 0);

	for (size_t idx = 0; idx < client->devices.length; idx++) {
		struct seat_device *seat_device = client->devices.items[idx];
		if (seat_device->device_id == device_id) {
			return seat_device;
		}
	}
	errno = ENOENT;
	return NULL;
}

struct seat_device *seat_open_device(struct client *client, const char *path) {
	assert(client);
	assert(client->seat);
	assert(strlen(path) > 0);
	struct seat *seat = client->seat;

	if (client != seat->active_client) {
		errno = EPERM;
		return NULL;
	}

	char sanitized_path[PATH_MAX];
	if (realpath(path, sanitized_path) == NULL) {
		log_errorf("invalid path '%s': %s", path, strerror(errno));
		return NULL;
	}

	int device_id = 1;
	for (size_t idx = 0; idx < client->devices.length; idx++) {
		struct seat_device *device = client->devices.items[idx];

		// If the device already exists, increase the ref count and
		// return it.
		if (strcmp(device->path, path) == 0) {
			device->ref_cnt++;
			return device;
		}

		// If the device has a higher id, up our device id
		if (device->device_id >= device_id) {
			device_id = device->device_id + 1;
		}
	}

	if (client->devices.length >= MAX_SEAT_DEVICES) {
		log_error("max seat devices exceeded");
		errno = EMFILE;
		return NULL;
	}

	const char *prefix = "/dev/";
	if (strncmp(prefix, sanitized_path, strlen(prefix)) != 0) {
		log_errorf("invalid path '%s': expected device in /dev", sanitized_path);
		errno = ENOENT;
		return NULL;
	}

	int fd = open(sanitized_path, O_RDWR | O_NOCTTY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
	if (fd == -1) {
		log_errorf("could not open file: %s", strerror(errno));
		return NULL;
	}

	struct stat st;
	if (fstat(fd, &st) == -1) {
		log_errorf("could not fstat: %s", strerror(errno));
		close(fd);
		errno = EACCES;
		return NULL;
	}

	if (dev_is_drm(st.st_rdev)) {
		if (drm_set_master(fd) == -1) {
			log_debugf("drm_set_master failed: %s", strerror(errno));
		}
	} else if (dev_is_evdev(st.st_rdev)) {
		// Nothing to do here
	} else {
		// Not a device type we want to share
		log_errorf("disallowed device type for '%s': %ld", sanitized_path, st.st_rdev);
		close(fd);
		errno = EACCES;
		return NULL;
	}

	struct seat_device *device = calloc(1, sizeof(struct seat_device));
	if (device == NULL) {
		log_errorf("could not alloc device for '%s': %s", sanitized_path, strerror(errno));
		close(fd);
		errno = ENOMEM;
		return NULL;
	}

	device->path = strdup(sanitized_path);
	if (device->path == NULL) {
		log_errorf("could not dup path for '%s': %s", sanitized_path, strerror(errno));
		close(fd);
		free(device);
		return NULL;
	}

	log_debugf("seat: %p, client: %p, path: '%s', device_id: %d", (void *)seat, (void *)client,
		   path, device_id);

	device->ref_cnt++;
	device->dev = st.st_rdev;
	device->fd = fd;
	device->device_id = device_id;
	device->active = true;
	list_add(&client->devices, device);
	return device;
}

int seat_close_device(struct client *client, struct seat_device *seat_device) {
	assert(client);
	assert(client->seat);
	assert(seat_device && seat_device->fd > 0);

	// Find the device in our list
	size_t idx = list_find(&client->devices, seat_device);
	if (idx == -1UL) {
		log_error("seat device not registered by client");
		errno = ENOENT;
		return -1;
	}

	log_debugf("seat: %p, client: %p, path: '%s', device_id: %d", (void *)client->seat,
		   (void *)client, seat_device->path, seat_device->device_id);

	seat_device->ref_cnt--;
	if (seat_device->ref_cnt > 0) {
		// We still have more references to this device, so leave it be.
		return 0;
	}

	// The ref count hit zero, so destroy the device
	list_del(&client->devices, idx);
	if (seat_device->active && seat_device->fd != -1) {
		if (dev_is_drm(seat_device->dev)) {
			if (drm_drop_master(seat_device->fd) == -1) {
				log_debugf("drm_drop_master failed: %s", strerror(errno));
			}
		} else if (dev_is_evdev(seat_device->dev)) {
			if (evdev_revoke(seat_device->fd) == -1) {
				log_debugf("evdev_revoke failed: %s", strerror(errno));
			}
		}
		close(seat_device->fd);
		seat_device->fd = -1;
	}
	free(seat_device->path);
	free(seat_device);
	return 0;
}

static int seat_deactivate_device(struct client *client, struct seat_device *seat_device) {
	assert(client);
	assert(client->seat);
	assert(seat_device && seat_device->fd > 0);

	if (!seat_device->active) {
		return 0;
	}
	if (dev_is_drm(seat_device->dev)) {
		if (drm_drop_master(seat_device->fd) == -1) {
			return -1;
		}
	} else if (dev_is_evdev(seat_device->dev)) {
		if (evdev_revoke(seat_device->fd) == -1) {
			return -1;
		}
	} else {
		errno = EACCES;
		return -1;
	}
	seat_device->active = false;
	return 0;
}

static int seat_activate_device(struct client *client, struct seat_device *seat_device) {
	assert(client);
	assert(client->seat);
	assert(seat_device && seat_device->fd > 0);

	if (seat_device->active) {
		return 0;
	}
	if (dev_is_drm(seat_device->dev)) {
		drm_set_master(seat_device->fd);
		seat_device->active = true;
	} else if (dev_is_evdev(seat_device->dev)) {
		// We can't do anything here
		errno = EINVAL;
		return -1;
	} else {
		errno = EACCES;
		return -1;
	}
	return 0;
}

int seat_open_client(struct seat *seat, struct client *client) {
	assert(seat);
	assert(client);

	if (seat->vt_bound && client->seat_vt == 0) {
		client->seat_vt = terminal_current_vt();
	}

	if (seat->active_client != NULL) {
		log_error("client already active");
		errno = EBUSY;
		return -1;
	}

	if (seat->vt_bound) {
		terminal_setup(client->seat_vt);
		terminal_set_keyboard(client->seat_vt, false);
	}

	for (size_t idx = 0; idx < client->devices.length; idx++) {
		struct seat_device *device = client->devices.items[idx];
		if (seat_activate_device(client, device) == -1) {
			log_errorf("unable to activate '%s': %s", device->path, strerror(errno));
		}
	}

	log_debugf("activated %zd devices", client->devices.length);

	seat->active_client = client;
	if (client_enable_seat(client) == -1) {
		seat_remove_client(seat, client);
		return -1;
	}

	log_info("client successfully enabled");
	return 0;
}

int seat_close_client(struct seat *seat, struct client *client) {
	assert(seat);
	assert(client);

	if (seat->active_client != client) {
		log_error("client not active");
		errno = EBUSY;
		return -1;
	}

	// We *deactivate* all remaining fds. These may later be reactivated.
	// The reason we cannot just close them is that certain device fds, such
	// as for DRM, must maintain the exact same file description for their
	// contexts to remain valid.
	for (size_t idx = 0; idx < client->devices.length; idx++) {
		struct seat_device *device = client->devices.items[idx];
		if (seat_deactivate_device(client, device) == -1) {
			log_errorf("unable to deactivate '%s': %s", device->path, strerror(errno));
		}
	}

	log_debugf("deactivated %zd devices", client->devices.length);

	int vt = seat->active_client->seat_vt;
	seat->active_client = NULL;

	if (seat->vt_bound) {
		if (seat->vt_pending_ack) {
			log_debug("acking pending VT switch");
			seat->vt_pending_ack = false;
			terminal_teardown(vt);
			terminal_ack_switch();
			return 0;
		}
	}

	seat_activate(seat);
	log_debug("closed client");
	return 0;
}

int seat_set_next_session(struct seat *seat, int session) {
	assert(seat);

	// Check if the session number is valid
	if (session <= 0) {
		errno = EINVAL;
		return -1;
	}

	// Check if a switch is already queued
	if (seat->next_vt > 0 || seat->next_client != NULL) {
		return 0;
	}

	struct client *target = NULL;
	for (size_t idx = 0; idx < seat->clients.length; idx++) {
		struct client *c = seat->clients.items[idx];
		if (client_get_session(c) == session) {
			target = c;
			break;
		}
	}

	if (target != NULL) {
		log_info("queuing switch to different client");
		seat->next_client = target;
		seat->next_vt = 0;
	} else if (seat->vt_bound) {
		log_info("queuing switch to different VT");
		seat->next_vt = session;
		seat->next_client = NULL;
	} else {
		log_error("no valid switch available");
		errno = EINVAL;
		return -1;
	}

	if (client_disable_seat(seat->active_client) == -1) {
		seat_remove_client(seat, seat->active_client);
	}

	return 0;
}

int seat_activate(struct seat *seat) {
	assert(seat);

	// We already have an active client!
	if (seat->active_client != NULL) {
		return 0;
	}

	// If we're asked to do a simple VT switch, do that
	if (seat->vt_bound && seat->next_vt > 0) {
		log_info("executing VT switch");
		terminal_switch_vt(seat->next_vt);
		seat->next_vt = 0;
		return 0;
	}

	int vt = -1;
	if (seat->vt_bound) {
		vt = terminal_current_vt();
	}

	// Try to pick a client for activation
	struct client *next_client = NULL;
	if (seat->next_client != NULL) {
		// A specific client has been requested, use it
		next_client = seat->next_client;
		seat->next_client = NULL;
	} else if (seat->clients.length > 0 && seat->vt_bound) {
		// No client is requested, try to find an applicable one
		for (size_t idx = 0; idx < seat->clients.length; idx++) {
			struct client *client = seat->clients.items[idx];
			if (client->seat_vt == vt) {
				next_client = client;
				break;
			}
		}
	} else if (seat->clients.length > 0) {
		next_client = seat->clients.items[0];
	}

	if (next_client == NULL) {
		// No suitable client found
		log_info("no client suitable for activation");
		if (seat->vt_bound) {
			terminal_teardown(vt);
		}
		return -1;
	}

	log_info("activating next client");
	if (seat->vt_bound && next_client->seat_vt != vt) {
		terminal_switch_vt(next_client->seat_vt);
	}

	return seat_open_client(seat, next_client);
}

int seat_prepare_vt_switch(struct seat *seat) {
	assert(seat);

	if (seat->active_client == NULL) {
		log_info("no active client, performing switch immediately");
		terminal_ack_switch();
		return 0;
	}

	if (seat->vt_pending_ack) {
		log_info("impatient user, killing session to force pending switch");
		seat_close_client(seat, seat->active_client);
		return 0;
	}

	log_debug("delaying VT switch acknowledgement");

	seat->vt_pending_ack = true;
	if (client_disable_seat(seat->active_client) == -1) {
		seat_remove_client(seat, seat->active_client);
	}

	return 0;
}
