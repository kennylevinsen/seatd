#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/un.h>
#include <unistd.h>

#if defined(HAVE_ELOGIND)
#include <elogind/sd-bus.h>
#include <elogind/sd-login.h>
#elif defined(HAVE_SYSTEMD)
#include <systemd/sd-bus.h>
#include <systemd/sd-login.h>
#else
#error logind backend requires either elogind or systemd
#endif

#include "backend.h"
#include "drm.h"
#include "libseat.h"
#include "log.h"

struct backend_logind {
	struct libseat base;
	struct libseat_seat_listener *seat_listener;
	void *seat_listener_data;

	sd_bus *bus;
	char *id;
	char *seat;
	char *path;
	char *seat_path;

	bool can_graphical;
	bool active;
	bool initial_setup;
	int has_drm;
};

const struct seat_impl logind_impl;
static struct backend_logind *backend_logind_from_libseat_backend(struct libseat *base);

static void destroy(struct backend_logind *backend) {
	assert(backend);
	if (backend->bus != NULL) {
		sd_bus_unref(backend->bus);
	}
	free(backend->id);
	free(backend->seat);
	free(backend->path);
	free(backend->seat_path);
	free(backend);
}

static int close_seat(struct libseat *base) {
	struct backend_logind *backend = backend_logind_from_libseat_backend(base);
	destroy(backend);
	return 0;
}

static int open_device(struct libseat *base, const char *path, int *fd) {
	struct backend_logind *session = backend_logind_from_libseat_backend(base);

	int ret;
	int tmpfd = -1;
	sd_bus_message *msg = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;

	struct stat st;
	if (stat(path, &st) < 0) {
		return -1;
	}

	ret = sd_bus_call_method(session->bus, "org.freedesktop.login1", session->path,
				 "org.freedesktop.login1.Session", "TakeDevice", &error, &msg, "uu",
				 major(st.st_rdev), minor(st.st_rdev));
	if (ret < 0) {
		tmpfd = -1;
		goto out;
	}

	int paused = 0;
	ret = sd_bus_message_read(msg, "hb", &tmpfd, &paused);
	if (ret < 0) {
		tmpfd = -1;
		goto out;
	}

	// The original fd seems to be closed when the message is freed
	// so we just clone it.
	tmpfd = fcntl(tmpfd, F_DUPFD_CLOEXEC, 0);
	if (tmpfd < 0) {
		tmpfd = -1;
		goto out;
	}

	if (dev_is_drm(st.st_rdev)) {
		session->has_drm++;
	}

	*fd = tmpfd;
out:
	sd_bus_error_free(&error);
	sd_bus_message_unref(msg);
	return tmpfd;
}

static int close_device(struct libseat *base, int device_id) {
	struct backend_logind *session = backend_logind_from_libseat_backend(base);
	if (device_id < 0) {
		errno = EINVAL;
		return -1;
	}

	int fd = device_id;

	struct stat st = {0};
	if (fstat(fd, &st) < 0) {
		close(fd);
		return -1;
	}
	if (dev_is_drm(st.st_rdev)) {
		session->has_drm--;
		assert(session->has_drm >= 0);
	}
	close(fd);

	sd_bus_message *msg = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;
	int ret = sd_bus_call_method(session->bus, "org.freedesktop.login1", session->path,
				     "org.freedesktop.login1.Session", "ReleaseDevice", &error,
				     &msg, "uu", major(st.st_rdev), minor(st.st_rdev));

	sd_bus_error_free(&error);
	sd_bus_message_unref(msg);

	return ret >= 0;
}

static int switch_session(struct libseat *base, int s) {
	struct backend_logind *session = backend_logind_from_libseat_backend(base);
	if (s < 0) {
		errno = EINVAL;
		return -1;
	}

	sd_bus_message *msg = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;

	int ret = sd_bus_call_method(session->bus, "org.freedesktop.login1",
				     "/org/freedesktop/login1/seat/seat0",
				     "org.freedesktop.login1.Seat", "SwitchTo", &error, &msg, "u",
				     (uint32_t)s);

	sd_bus_error_free(&error);
	sd_bus_message_unref(msg);
	return ret >= 0;
}

static int disable_seat(struct libseat *base) {
	(void)base;
	return 0;
}

static int get_fd(struct libseat *base) {
	struct backend_logind *backend = backend_logind_from_libseat_backend(base);
	return sd_bus_get_fd(backend->bus);
}

static int poll_connection(struct backend_logind *backend, int timeout) {
	struct pollfd fd = {
		.fd = sd_bus_get_fd(backend->bus),
		.events = POLLIN,
	};

	if (poll(&fd, 1, timeout) == -1) {
		if (errno == EAGAIN || errno == EINTR) {
			return 0;
		} else {
			return -1;
		}
	}

	if (fd.revents & (POLLERR | POLLHUP)) {
		errno = ECONNRESET;
		return -1;
	}
	return 0;
}

static int dispatch_background(struct libseat *base, int timeout) {
	struct backend_logind *backend = backend_logind_from_libseat_backend(base);
	if (backend->initial_setup) {
		backend->initial_setup = false;
		if (backend->active) {
			backend->seat_listener->enable_seat(&backend->base,
							    backend->seat_listener_data);
		} else {
			backend->seat_listener->disable_seat(&backend->base,
							     backend->seat_listener_data);
		}
	}

	int total_dispatched = 0;
	int dispatched = 0;
	while ((dispatched = sd_bus_process(backend->bus, NULL)) > 0) {
		total_dispatched += dispatched;
	}
	if (total_dispatched == 0 && timeout != 0) {
		if (poll_connection(backend, timeout) == -1) {
			return -1;
		}
		while ((dispatched = sd_bus_process(backend->bus, NULL)) > 0) {
			total_dispatched += dispatched;
		}
	}
	return total_dispatched;
}

static const char *seat_name(struct libseat *base) {
	struct backend_logind *backend = backend_logind_from_libseat_backend(base);

	if (backend->seat == NULL) {
		return NULL;
	}
	return backend->seat;
}

static struct backend_logind *backend_logind_from_libseat_backend(struct libseat *base) {
	assert(base->impl == &logind_impl);
	return (struct backend_logind *)base;
}

static bool session_activate(struct backend_logind *session) {
	sd_bus_message *msg = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;

	int ret = sd_bus_call_method(session->bus, "org.freedesktop.login1", session->path,
				     "org.freedesktop.login1.Session", "Activate", &error, &msg, "");

	sd_bus_error_free(&error);
	sd_bus_message_unref(msg);
	return ret >= 0;
}

static bool take_control(struct backend_logind *session) {
	sd_bus_message *msg = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;

	int ret = sd_bus_call_method(session->bus, "org.freedesktop.login1", session->path,
				     "org.freedesktop.login1.Session", "TakeControl", &error, &msg,
				     "b", false);

	sd_bus_error_free(&error);
	sd_bus_message_unref(msg);
	return ret >= 0;
}

static void set_active(struct backend_logind *backend, bool active) {
	if (backend->active == active) {
		return;
	}

	backend->active = active;
	if (active) {
		backend->seat_listener->enable_seat(&backend->base, backend->seat_listener_data);
	} else {
		backend->seat_listener->disable_seat(&backend->base, backend->seat_listener_data);
	}
}

static int pause_device(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error) {
	struct backend_logind *session = userdata;

	uint32_t major, minor;
	const char *type;
	int ret = sd_bus_message_read(msg, "uus", &major, &minor, &type);
	if (ret < 0) {
		goto error;
	}

	if (dev_is_drm(makedev(major, minor)) && strcmp(type, "gone") != 0) {
		assert(session->has_drm > 0);
		set_active(session, false);
	}

	if (strcmp(type, "pause") == 0) {
		sd_bus_call_method(session->bus, "org.freedesktop.login1", session->path,
				   "org.freedesktop.login1.Session", "PauseDeviceComplete",
				   ret_error, &msg, "uu", major, minor);
	}

error:
	return 0;
}

static int resume_device(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error) {
	(void)ret_error;
	struct backend_logind *session = userdata;
	int ret;

	int fd;
	uint32_t major, minor;
	ret = sd_bus_message_read(msg, "uuh", &major, &minor, &fd);
	if (ret < 0) {
		goto error;
	}

	if (dev_is_drm(makedev(major, minor))) {
		assert(session->has_drm > 0);
		set_active(session, true);
	}

error:
	return 0;
}

static int session_properties_changed(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error) {
	(void)ret_error;
	struct backend_logind *session = userdata;
	int ret = 0;

	if (session->has_drm > 0) {
		return 0;
	}

	// PropertiesChanged arg 1: interface
	const char *interface;
	ret = sd_bus_message_read_basic(msg, 's', &interface); // skip path
	if (ret < 0) {
		goto error;
	}

	if (strcmp(interface, "org.freedesktop.login1.Session") != 0) {
		// not interesting for us; ignore
		return 0;
	}

	// PropertiesChanged arg 2: changed properties with values
	ret = sd_bus_message_enter_container(msg, 'a', "{sv}");
	if (ret < 0) {
		goto error;
	}

	const char *s;
	while ((ret = sd_bus_message_enter_container(msg, 'e', "sv")) > 0) {
		ret = sd_bus_message_read_basic(msg, 's', &s);
		if (ret < 0) {
			goto error;
		}

		if (strcmp(s, "Active") == 0) {
			int ret;
			ret = sd_bus_message_enter_container(msg, 'v', "b");
			if (ret < 0) {
				goto error;
			}

			bool active;
			ret = sd_bus_message_read_basic(msg, 'b', &active);
			if (ret < 0) {
				goto error;
			}

			set_active(session, active);
			return 0;
		} else {
			sd_bus_message_skip(msg, "{sv}");
		}

		ret = sd_bus_message_exit_container(msg);
		if (ret < 0) {
			goto error;
		}
	}

	if (ret < 0) {
		goto error;
	}

	ret = sd_bus_message_exit_container(msg);
	if (ret < 0) {
		goto error;
	}

	// PropertiesChanged arg 3: changed properties without values
	sd_bus_message_enter_container(msg, 'a', "s");
	while ((ret = sd_bus_message_read_basic(msg, 's', &s)) > 0) {
		if (strcmp(s, "Active") == 0) {
			sd_bus_error error = SD_BUS_ERROR_NULL;
			bool active;
			ret = sd_bus_get_property_trivial(session->bus, "org.freedesktop.login1",
							  session->path,
							  "org.freedesktop.login1.Session",
							  "Active", &error, 'b', &active);
			if (ret < 0) {
				return 0;
			}

			set_active(session, active);
			return 0;
		}
	}

	if (ret < 0) {
		goto error;
	}

	return 0;

error:
	return 0;
}

static int seat_properties_changed(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error) {
	(void)ret_error;
	struct backend_logind *session = userdata;
	int ret = 0;

	if (session->has_drm > 0) {
		return 0;
	}

	// PropertiesChanged arg 1: interface
	const char *interface;
	ret = sd_bus_message_read_basic(msg, 's', &interface); // skip path
	if (ret < 0) {
		goto error;
	}

	if (strcmp(interface, "org.freedesktop.login1.Seat") != 0) {
		// not interesting for us; ignore
		return 0;
	}

	// PropertiesChanged arg 2: changed properties with values
	ret = sd_bus_message_enter_container(msg, 'a', "{sv}");
	if (ret < 0) {
		goto error;
	}

	const char *s;
	while ((ret = sd_bus_message_enter_container(msg, 'e', "sv")) > 0) {
		ret = sd_bus_message_read_basic(msg, 's', &s);
		if (ret < 0) {
			goto error;
		}

		if (strcmp(s, "CanGraphical") == 0) {
			int ret;
			ret = sd_bus_message_enter_container(msg, 'v', "b");
			if (ret < 0) {
				goto error;
			}

			ret = sd_bus_message_read_basic(msg, 'b', &session->can_graphical);
			if (ret < 0) {
				goto error;
			}

			return 0;
		} else {
			sd_bus_message_skip(msg, "{sv}");
		}

		ret = sd_bus_message_exit_container(msg);
		if (ret < 0) {
			goto error;
		}
	}

	if (ret < 0) {
		goto error;
	}

	ret = sd_bus_message_exit_container(msg);
	if (ret < 0) {
		goto error;
	}

	// PropertiesChanged arg 3: changed properties without values
	sd_bus_message_enter_container(msg, 'a', "s");
	while ((ret = sd_bus_message_read_basic(msg, 's', &s)) > 0) {
		if (strcmp(s, "CanGraphical") == 0) {
			session->can_graphical = sd_seat_can_graphical(session->seat);
			return 0;
		}
	}

	if (ret < 0) {
		goto error;
	}

	return 0;

error:
	return 0;
}

static bool add_signal_matches(struct backend_logind *backend) {
	static const char *logind = "org.freedesktop.login1";
	static const char *session_interface = "org.freedesktop.login1.Session";
	static const char *property_interface = "org.freedesktop.DBus.Properties";
	int ret;

	ret = sd_bus_match_signal(backend->bus, NULL, logind, backend->path, session_interface,
				  "PauseDevice", pause_device, backend);
	if (ret < 0) {
		return false;
	}

	ret = sd_bus_match_signal(backend->bus, NULL, logind, backend->path, session_interface,
				  "ResumeDevice", resume_device, backend);
	if (ret < 0) {
		return false;
	}

	ret = sd_bus_match_signal(backend->bus, NULL, logind, backend->path, property_interface,
				  "PropertiesChanged", session_properties_changed, backend);
	if (ret < 0) {
		return false;
	}

	ret = sd_bus_match_signal(backend->bus, NULL, logind, backend->seat_path, property_interface,
				  "PropertiesChanged", seat_properties_changed, backend);
	if (ret < 0) {
		return false;
	}

	return true;
}

static bool contains_str(const char *needle, const char **haystack) {
	for (int i = 0; haystack[i]; i++) {
		if (strcmp(haystack[i], needle) == 0) {
			return true;
		}
	}

	return false;
}

static bool get_greeter_session(char **session_id) {
	char *class = NULL;
	char **user_sessions = NULL;
	int user_session_count = sd_uid_get_sessions(getuid(), 1, &user_sessions);

	if (user_session_count < 0) {
		goto out;
	}

	if (user_session_count == 0) {
		goto out;
	}

	for (int i = 0; i < user_session_count; ++i) {
		int ret = sd_session_get_class(user_sessions[i], &class);
		if (ret < 0) {
			continue;
		}

		if (strcmp(class, "greeter") == 0) {
			*session_id = strdup(user_sessions[i]);
			goto out;
		}
	}

out:
	free(class);
	for (int i = 0; i < user_session_count; ++i) {
		free(user_sessions[i]);
	}
	free(user_sessions);

	return *session_id != NULL;
}

static bool find_session_path(struct backend_logind *session) {
	int ret;
	sd_bus_message *msg = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;

	ret = sd_bus_call_method(session->bus, "org.freedesktop.login1", "/org/freedesktop/login1",
				 "org.freedesktop.login1.Manager", "GetSession", &error, &msg, "s",
				 session->id);
	if (ret < 0) {
		goto out;
	}

	const char *path;
	ret = sd_bus_message_read(msg, "o", &path);
	if (ret < 0) {
		goto out;
	}
	session->path = strdup(path);

out:
	sd_bus_error_free(&error);
	sd_bus_message_unref(msg);

	return ret >= 0;
}

static bool find_seat_path(struct backend_logind *session) {
	int ret;
	sd_bus_message *msg = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;

	ret = sd_bus_call_method(session->bus, "org.freedesktop.login1", "/org/freedesktop/login1",
				 "org.freedesktop.login1.Manager", "GetSeat", &error, &msg, "s",
				 session->seat);
	if (ret < 0) {
		goto out;
	}

	const char *path;
	ret = sd_bus_message_read(msg, "o", &path);
	if (ret < 0) {
		goto out;
	}
	session->seat_path = strdup(path);

out:
	sd_bus_error_free(&error);
	sd_bus_message_unref(msg);

	return ret >= 0;
}

static bool get_display_session(char **session_id) {
	assert(session_id != NULL);
	char *xdg_session_id = getenv("XDG_SESSION_ID");
	char *state = NULL;

	if (xdg_session_id) {
		// This just checks whether the supplied session ID is valid
		if (sd_session_is_active(xdg_session_id) < 0) {
			goto error;
		}
		*session_id = strdup(xdg_session_id);
		return true;
	}

	// If there's a session active for the current process then just use
	// that
	int ret = sd_pid_get_session(getpid(), session_id);
	if (ret == 0) {
		return true;
	}

	// Find any active sessions for the user if the process isn't part of an
	// active session itself
	ret = sd_uid_get_display(getuid(), session_id);
	if (ret < 0 && ret != -ENODATA) {
		goto error;
	}

	if (ret != 0 && !get_greeter_session(session_id)) {
		goto error;
	}

	assert(*session_id != NULL);

	// Check that the session is active
	ret = sd_session_get_state(*session_id, &state);
	if (ret < 0) {
		goto error;
	}

	const char *active_states[] = {"active", "online", NULL};
	if (!contains_str(state, active_states)) {
		goto error;
	}

	free(state);
	return true;

error:
	free(state);
	free(*session_id);
	*session_id = NULL;

	return false;
}

static int set_type(struct backend_logind *backend, const char *type) {
	sd_bus_message *msg = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;

	int ret = sd_bus_call_method(backend->bus, "org.freedesktop.login1", backend->path,
				     "org.freedesktop.login1.Session", "SetType", &error, &msg, "s",
				     type);
	if (ret < 0) {
		log_errorf("unable to set session type: %s", error.message);
	}

	sd_bus_error_free(&error);
	sd_bus_message_unref(msg);
	return ret;
}

static struct libseat *logind_open_seat(struct libseat_seat_listener *listener, void *data) {
	struct backend_logind *backend = calloc(1, sizeof(struct backend_logind));
	if (backend == NULL) {
		return NULL;
	}

	if (!get_display_session(&backend->id)) {
		goto error;
	}

	int ret = sd_session_get_seat(backend->id, &backend->seat);
	if (ret < 0) {
		goto error;
	}

	ret = sd_bus_default_system(&backend->bus);
	if (ret < 0) {
		goto error;
	}

	if (!find_session_path(backend)) {
		goto error;
	}

	if (!find_seat_path(backend)) {
		goto error;
	}

	if (!add_signal_matches(backend)) {
		goto error;
	}

	if (!session_activate(backend)) {
		goto error;
	}

	if (!take_control(backend)) {
		goto error;
	}

	backend->can_graphical = sd_seat_can_graphical(backend->seat);
	while (!backend->can_graphical) {
		if (poll_connection(backend, -1) == -1) {
			goto error;
		}
	}

	const char *env = getenv("XDG_SESSION_TYPE");
	if (env != NULL) {
		set_type(backend, env);
	}

	backend->initial_setup = true;
	backend->active = true;
	backend->seat_listener = listener;
	backend->seat_listener_data = data;
	backend->base.impl = &logind_impl;

	return &backend->base;

error:
	if (backend != NULL) {
		destroy(backend);
	}
	return NULL;
}

const struct seat_impl logind_impl = {
	.open_seat = logind_open_seat,
	.disable_seat = disable_seat,
	.close_seat = close_seat,
	.seat_name = seat_name,
	.open_device = open_device,
	.close_device = close_device,
	.switch_session = switch_session,
	.get_fd = get_fd,
	.dispatch = dispatch_background,
};
