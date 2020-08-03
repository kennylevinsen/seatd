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
#include "linked_list.h"
#include "log.h"
#include "protocol.h"
#include "seat.h"
#include "terminal.h"

struct seat *seat_create(const char *seat_name, bool vt_bound) {
	struct seat *seat = calloc(1, sizeof(struct seat));
	if (seat == NULL) {
		return NULL;
	}
	linked_list_init(&seat->clients);
	seat->vt_bound = vt_bound;
	seat->curttyfd = -1;
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
	while (!linked_list_empty(&seat->clients)) {
		struct client *client = (struct client *)seat->clients.next;
		// This will cause the client to remove itself from the seat
		assert(client->seat);
		client_kill(client);
	}
	assert(seat->curttyfd == -1);

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

	linked_list_insert(&seat->clients, &client->link);
	log_debug("added client");
	return 0;
}

int seat_remove_client(struct client *client) {
	assert(client);
	assert(client->seat);

	struct seat *seat = client->seat;

	// We must first remove the client to avoid reactivation
	linked_list_remove(&client->link);

	if (seat->next_client == client) {
		seat->next_client = NULL;
	}

	while (!linked_list_empty(&client->devices)) {
		struct seat_device *device = (struct seat_device *)client->devices.next;
		seat_close_device(client, device);
		linked_list_remove(&device->link);
	}

	if (seat->active_client == client) {
		seat_close_client(client);
	}

	client->seat = NULL;
	log_debug("removed client");

	return 0;
}

struct seat_device *seat_find_device(struct client *client, int device_id) {
	assert(client);
	assert(client->seat);
	assert(device_id != 0);

	for (struct linked_list *elem = client->devices.next; elem != &client->devices;
	     elem = elem->next) {
		struct seat_device *seat_device = (struct seat_device *)elem;
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

	if (client->pending_disable) {
		errno = EPERM;
		return NULL;
	}

	char sanitized_path[PATH_MAX];
	if (realpath(path, sanitized_path) == NULL) {
		log_errorf("invalid path '%s': %s", path, strerror(errno));
		return NULL;
	}

	enum seat_device_type type;
	if (path_is_evdev(sanitized_path)) {
		type = SEAT_DEVICE_TYPE_EVDEV;
	} else if (path_is_drm(sanitized_path)) {
		type = SEAT_DEVICE_TYPE_DRM;
	} else {
		log_errorf("invalid path '%s'", sanitized_path);
		errno = ENOENT;
		return NULL;
	}

	int device_id = 1;
	size_t device_count = 0;
	for (struct linked_list *elem = client->devices.next; elem != &client->devices;
	     elem = elem->next) {
		struct seat_device *device = (struct seat_device *)elem;

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
		device_count++;
	}

	if (device_count >= MAX_SEAT_DEVICES) {
		log_error("max seat devices exceeded");
		errno = EMFILE;
		return NULL;
	}

	int fd = open(sanitized_path, O_RDWR | O_NOCTTY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
	if (fd == -1) {
		log_errorf("could not open file: %s", strerror(errno));
		return NULL;
	}

	switch (type) {
	case SEAT_DEVICE_TYPE_DRM:
		if (drm_set_master(fd) == -1) {
			log_debugf("drm_set_master failed: %s", strerror(errno));
		}
		break;
	case SEAT_DEVICE_TYPE_EVDEV:
		// Nothing to do here
		break;
	default:
		log_error("invalid seat device type");
		abort();
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

	device->ref_cnt = 1;
	device->type = type;
	device->fd = fd;
	device->device_id = device_id;
	device->active = true;
	linked_list_insert(&client->devices, &device->link);
	return device;
}

int seat_close_device(struct client *client, struct seat_device *seat_device) {
	assert(client);
	assert(client->seat);
	assert(seat_device && seat_device->fd != -1);

	log_debugf("seat: %p, client: %p, path: '%s', device_id: %d", (void *)client->seat,
		   (void *)client, seat_device->path, seat_device->device_id);

	seat_device->ref_cnt--;
	if (seat_device->ref_cnt > 0) {
		// We still have more references to this device, so leave it be.
		return 0;
	}

	// The ref count hit zero, so destroy the device
	linked_list_remove(&seat_device->link);
	if (seat_device->active && seat_device->fd != -1) {
		switch (seat_device->type) {
		case SEAT_DEVICE_TYPE_DRM:
			if (drm_drop_master(seat_device->fd) == -1) {
				log_debugf("drm_drop_master failed: %s", strerror(errno));
			}
			break;
		case SEAT_DEVICE_TYPE_EVDEV:
			if (evdev_revoke(seat_device->fd) == -1) {
				log_debugf("evdev_revoke failed: %s", strerror(errno));
			}
			break;
		default:
			log_error("invalid seat device type");
			abort();
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
	switch (seat_device->type) {
	case SEAT_DEVICE_TYPE_DRM:
		if (drm_drop_master(seat_device->fd) == -1) {
			log_debugf("drm_drop_master failed: %s", strerror(errno));
			return -1;
		}
		break;
	case SEAT_DEVICE_TYPE_EVDEV:
		if (evdev_revoke(seat_device->fd) == -1) {
			log_debugf("evdev_revoke failed: %s", strerror(errno));
			return -1;
		}
		break;
	default:
		log_error("invalid seat device type");
		abort();
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
	switch (seat_device->type) {
	case SEAT_DEVICE_TYPE_DRM:
		if (drm_set_master(seat_device->fd) == -1) {
			log_debugf("drmset_master failed: %s", strerror(errno));
		}
		seat_device->active = true;
		break;
	case SEAT_DEVICE_TYPE_EVDEV:
		errno = EINVAL;
		return -1;
	default:
		log_error("invalid seat device type");
		abort();
	}

	return 0;
}

int seat_open_client(struct seat *seat, struct client *client) {
	assert(seat);
	assert(client);

	if (seat->vt_bound && client->seat_vt == 0) {
		int tty0fd = terminal_open(0);
		if (tty0fd == -1) {
			log_errorf("unable to open tty0: %s", strerror(errno));
			return -1;
		}
		client->seat_vt = terminal_current_vt(tty0fd);
		close(tty0fd);
		if (client->seat_vt == -1) {
			log_errorf("unable to get current VT for client: %s", strerror(errno));
			client->seat_vt = 0;
			return -1;
		}
	}

	if (seat->active_client != NULL) {
		log_error("client already active");
		errno = EBUSY;
		return -1;
	}

	assert(seat->curttyfd == -1);

	if (seat->vt_bound) {
		int ttyfd = terminal_open(client->seat_vt);
		if (ttyfd == -1) {
			log_errorf("unable to open tty for vt %d: %s", client->seat_vt,
				   strerror(errno));
			return -1;
		}
		terminal_set_process_switching(ttyfd, true);
		terminal_set_keyboard(ttyfd, false);
		terminal_set_graphics(ttyfd, true);
		seat->curttyfd = ttyfd;
	}

	for (struct linked_list *elem = client->devices.next; elem != &client->devices;
	     elem = elem->next) {
		struct seat_device *device = (struct seat_device *)elem;
		if (seat_activate_device(client, device) == -1) {
			log_errorf("unable to activate '%s': %s", device->path, strerror(errno));
		}
	}

	seat->active_client = client;
	if (client_send_enable_seat(client) == -1) {
		seat_remove_client(client);
		return -1;
	}

	log_info("client successfully enabled");
	return 0;
}

int seat_close_client(struct client *client) {
	assert(client);
	assert(client->seat);

	struct seat *seat = client->seat;

	if (seat->active_client != client) {
		log_error("client not active");
		errno = EBUSY;
		return -1;
	}

	while (!linked_list_empty(&client->devices)) {
		struct seat_device *device = (struct seat_device *)client->devices.next;
		if (seat_close_device(client, device) == -1) {
			log_errorf("unable to close '%s': %s", device->path, strerror(errno));
		}
		linked_list_remove(&device->link);
	}

	client->pending_disable = false;
	seat->active_client = NULL;
	seat_activate(seat);
	log_debug("closed client");
	return 0;
}

static int seat_disable_client(struct client *client) {
	assert(client);
	assert(client->seat);

	struct seat *seat = client->seat;

	if (seat->active_client != client) {
		log_error("client not active");
		errno = EBUSY;
		return -1;
	}

	// We *deactivate* all remaining fds. These may later be reactivated.
	// The reason we cannot just close them is that certain device fds, such
	// as for DRM, must maintain the exact same file description for their
	// contexts to remain valid.
	for (struct linked_list *elem = client->devices.next; elem != &client->devices;
	     elem = elem->next) {
		struct seat_device *device = (struct seat_device *)elem;
		if (seat_deactivate_device(client, device) == -1) {
			log_errorf("unable to deactivate '%s': %s", device->path, strerror(errno));
		}
	}

	client->pending_disable = true;
	if (client_send_disable_seat(seat->active_client) == -1) {
		seat_remove_client(client);
		return -1;
	}

	log_debug("disabling client");
	return 0;
}

int seat_ack_disable_client(struct client *client) {
	assert(client);
	assert(client->seat);

	struct seat *seat = client->seat;

	if (seat->active_client != client || !client->pending_disable) {
		log_error("client not active or not pending disable");
		errno = EBUSY;
		return -1;
	}

	client->pending_disable = false;
	seat->active_client = NULL;
	seat_activate(seat);
	log_debug("disabled client");
	return 0;
}

int seat_set_next_session(struct client *client, int session) {
	assert(client);
	assert(client->seat);

	struct seat *seat = client->seat;

	if (seat->active_client != client || client->pending_disable) {
		log_error("client not active or pending disable");
		errno = EPERM;
		return -1;
	}

	if (session == client_get_session(client)) {
		log_info("requested session is already active");
		return 0;
	}

	// Check if the session number is valid
	if (session <= 0) {
		log_errorf("invalid session value: %d", session);
		errno = EINVAL;
		return -1;
	}

	// Check if a switch is already queued
	if (seat->next_vt > 0 || seat->next_client != NULL) {
		log_info("switch is already queued");
		return 0;
	}

	struct client *target = NULL;

	for (struct linked_list *elem = seat->clients.next; elem != &seat->clients;
	     elem = elem->next) {
		struct client *c = (struct client *)elem;
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

	seat_disable_client(seat->active_client);
	return 0;
}

int seat_activate(struct seat *seat) {
	assert(seat);

	// We already have an active client!
	if (seat->active_client != NULL) {
		return 0;
	}

	int vt = -1;
	if (seat->vt_bound) {
		int ttyfd = terminal_open(0);
		if (ttyfd == -1) {
			log_errorf("unable to open tty0: %s", strerror(errno));
			return -1;
		}

		// If we need to ack a switch, do that
		if (seat->vt_pending_ack) {
			log_info("acking pending VT switch");
			seat->vt_pending_ack = false;
			if (seat->curttyfd != -1) {
				terminal_set_process_switching(seat->curttyfd, true);
				terminal_set_keyboard(seat->curttyfd, true);
				terminal_set_graphics(seat->curttyfd, false);
				close(seat->curttyfd);
				seat->curttyfd = -1;
			}
			return 0;
		}

		// If we're asked to do a simple VT switch, do that
		if (seat->next_vt > 0) {
			log_info("executing VT switch");
			if (seat->curttyfd != -1) {
				terminal_set_process_switching(seat->curttyfd, true);
				terminal_set_keyboard(seat->curttyfd, true);
				terminal_set_graphics(seat->curttyfd, false);
				close(seat->curttyfd);
				seat->curttyfd = -1;
			}
			terminal_switch_vt(ttyfd, seat->next_vt);
			seat->next_vt = 0;
			close(ttyfd);
			return 0;
		}

		// We'll need the VT below
		vt = terminal_current_vt(ttyfd);
		if (vt == -1) {
			log_errorf("unable to get vt: %s", strerror(errno));
			close(ttyfd);
			return -1;
		}
		close(ttyfd);
	}

	// Try to pick a client for activation
	struct client *next_client = NULL;
	if (seat->next_client != NULL) {
		// A specific client has been requested, use it
		next_client = seat->next_client;
		seat->next_client = NULL;
	} else if (!linked_list_empty(&seat->clients) && seat->vt_bound) {
		// No client is requested, try to find an applicable one
		for (struct linked_list *elem = seat->clients.next; elem != &seat->clients;
		     elem = elem->next) {
			struct client *client = (struct client *)elem;
			if (client->seat_vt == vt) {
				next_client = client;
				break;
			}
		}
	} else if (!linked_list_empty(&seat->clients)) {
		next_client = (struct client *)seat->clients.next;
	}

	if (next_client == NULL) {
		// No suitable client found
		log_info("no client suitable for activation");
		if (seat->vt_bound && seat->curttyfd != -1) {
			terminal_set_process_switching(seat->curttyfd, false);
			terminal_set_keyboard(seat->curttyfd, true);
			terminal_set_graphics(seat->curttyfd, false);
			close(seat->curttyfd);
			seat->curttyfd = -1;
		}
		return -1;
	}

	log_info("activating next client");
	if (seat->vt_bound && next_client->seat_vt != vt) {
		int ttyfd = terminal_open(0);
		if (ttyfd == -1) {
			log_errorf("unable to open tty0: %s", strerror(errno));
			return -1;
		}
		terminal_switch_vt(ttyfd, next_client->seat_vt);
		close(ttyfd);
	}

	return seat_open_client(seat, next_client);
}

int seat_prepare_vt_switch(struct seat *seat) {
	assert(seat);

	if (seat->active_client == NULL) {
		log_info("no active client, performing switch immediately");
		int tty0fd = terminal_open(0);
		if (tty0fd == -1) {
			log_errorf("unable to open tty0: %s", strerror(errno));
			return -1;
		}
		terminal_ack_switch(tty0fd);
		close(tty0fd);
		return 0;
	}

	if (seat->vt_pending_ack) {
		log_info("impatient user, killing session to force pending switch");
		seat_close_client(seat->active_client);
		return 0;
	}

	log_debug("delaying VT switch acknowledgement");

	seat->vt_pending_ack = true;
	seat_disable_client(seat->active_client);
	return 0;
}
