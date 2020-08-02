#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <signal.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "client.h"
#include "list.h"
#include "log.h"
#include "poller.h"
#include "seat.h"
#include "server.h"
#include "terminal.h"

#define LISTEN_BACKLOG 16

static int server_handle_vt_acq(int signal, void *data);
static int server_handle_vt_rel(int signal, void *data);
static int server_handle_kill(int signal, void *data);

int server_init(struct server *server) {
	poller_init(&server->poller);

	list_init(&server->seats);

	if (poller_add_signal(&server->poller, SIGUSR1, server_handle_vt_rel, server) == NULL ||
	    poller_add_signal(&server->poller, SIGUSR2, server_handle_vt_acq, server) == NULL ||
	    poller_add_signal(&server->poller, SIGINT, server_handle_kill, server) == NULL ||
	    poller_add_signal(&server->poller, SIGTERM, server_handle_kill, server) == NULL) {
		server_finish(server);
		return -1;
	}

	char *vtenv = getenv("SEATD_VTBOUND");

	// TODO: create more seats:
	struct seat *seat = seat_create("seat0", vtenv == NULL || strcmp(vtenv, "1") == 0);
	if (seat == NULL) {
		server_finish(server);
		return -1;
	}

	list_add(&server->seats, seat);
	server->running = true;
	return 0;
}

void server_finish(struct server *server) {
	assert(server);
	for (size_t idx = 0; idx < server->seats.length; idx++) {
		struct seat *seat = server->seats.items[idx];
		seat_destroy(seat);
	}
	list_free(&server->seats);
	poller_finish(&server->poller);
}

struct seat *server_get_seat(struct server *server, const char *seat_name) {
	for (size_t idx = 0; idx < server->seats.length; idx++) {
		struct seat *seat = server->seats.items[idx];
		if (strcmp(seat->seat_name, seat_name) == 0) {
			return seat;
		}
	}
	return NULL;
}

static int server_handle_vt_acq(int signal, void *data) {
	(void)signal;
	struct server *server = data;
	struct seat *seat = server_get_seat(server, "seat0");
	if (seat == NULL) {
		return -1;
	}

	seat_activate(seat);
	return 0;
}

static int server_handle_vt_rel(int signal, void *data) {
	(void)signal;
	struct server *server = data;
	struct seat *seat = server_get_seat(server, "seat0");
	if (seat == NULL) {
		return -1;
	}

	seat_prepare_vt_switch(seat);
	return 0;
}

static int server_handle_kill(int signal, void *data) {
	(void)signal;
	struct server *server = data;
	server->running = false;
	return 0;
}

static int set_nonblock(int fd) {
	int flags;
	if ((flags = fcntl(fd, F_GETFD)) == -1 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1) {
		log_errorf("could not set FD_CLOEXEC on socket: %s", strerror(errno));
		return -1;
	}
	if ((flags = fcntl(fd, F_GETFL)) == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
		log_errorf("could not set O_NONBLOCK on socket: %s", strerror(errno));
		return -1;
	}
	return 0;
}

static int server_handle_connection(int fd, uint32_t mask, void *data) {
	struct server *server = data;
	if (mask & (EVENT_ERROR | EVENT_HANGUP)) {
		close(fd);
		log_errorf("server socket recieved an error: %s", strerror(errno));
		exit(1);
	}

	if (mask & EVENT_READABLE) {
		int new_fd = accept(fd, NULL, NULL);
		if (fd == -1) {
			log_errorf("could not accept client connection: %s", strerror(errno));
			return 0;
		}

		if (set_nonblock(new_fd) != 0) {
			close(new_fd);
			log_errorf("could not prepare new client socket: %s", strerror(errno));
			return 0;
		}

		struct client *client = client_create(server, new_fd);
		client->event_source = poller_add_fd(&server->poller, new_fd, EVENT_READABLE,
						     client_handle_connection, client);
		if (client->event_source == NULL) {
			client_destroy(client);
			log_errorf("could not add client socket to poller: %s", strerror(errno));
			return 0;
		}
		log_infof("new client connected (pid: %d, uid: %d, gid: %d)", client->pid,
			  client->uid, client->gid);
	}
	return 0;
}

int server_add_client(struct server *server, int fd) {
	if (set_nonblock(fd) != 0) {
		close(fd);
		log_errorf("could not prepare new client socket: %s", strerror(errno));
		return -1;
	}

	struct client *client = client_create(server, fd);
	client->event_source =
		poller_add_fd(&server->poller, fd, EVENT_READABLE, client_handle_connection, client);
	if (client->event_source == NULL) {
		client_destroy(client);
		log_errorf("could not add client socket to poller: %s", strerror(errno));
		return -1;
	}
	return 0;
}

int server_listen(struct server *server, const char *path) {
	union {
		struct sockaddr_un unix;
		struct sockaddr generic;
	} addr = {{0}};
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd == -1) {
		log_errorf("could not create socket: %s", strerror(errno));
		return -1;
	}
	if (set_nonblock(fd) == -1) {
		close(fd);
		log_errorf("could not prepare socket: %s", strerror(errno));
		return -1;
	}

	addr.unix.sun_family = AF_UNIX;
	strncpy(addr.unix.sun_path, path, sizeof addr.unix.sun_path - 1);
	socklen_t size = offsetof(struct sockaddr_un, sun_path) + strlen(addr.unix.sun_path);
	if (bind(fd, &addr.generic, size) == -1) {
		log_errorf("could not bind socket: %s", strerror(errno));
		close(fd);
		return -1;
	}
	if (listen(fd, LISTEN_BACKLOG) == -1) {
		log_errorf("could not listen on socket: %s", strerror(errno));
		close(fd);
		return -1;
	}
	struct group *videogrp = getgrnam("video");
	if (videogrp != NULL) {
		if (chown(path, 0, videogrp->gr_gid) == -1) {
			log_errorf("could not chown socket to video group: %s", strerror(errno));
		} else if (chmod(path, 0770) == -1) {
			log_errorf("could not chmod socket: %s", strerror(errno));
		}
	} else {
		log_errorf("could not get video group: %s", strerror(errno));
	}
	if (poller_add_fd(&server->poller, fd, EVENT_READABLE, server_handle_connection, server) ==
	    NULL) {
		log_errorf("could not add socket to poller: %s", strerror(errno));
		close(fd);
		return -1;
	}
	return 0;
}
