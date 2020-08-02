#ifndef _SEATD_CLIENT_H
#define _SEATD_CLIENT_H

#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>

#include "connection.h"
#include "list.h"

struct server;

struct client {
	struct server *server;
	struct event_source_fd *event_source;
	struct connection connection;

	pid_t pid;
	uid_t uid;
	gid_t gid;

	struct seat *seat;
	int seat_vt;
	bool pending_disable;

	struct list devices;
};

struct client *client_create(struct server *server, int client_fd);
void client_kill(struct client *client);
void client_destroy(struct client *client);

int client_handle_connection(int fd, uint32_t mask, void *data);
int client_get_session(struct client *client);
int client_enable_seat(struct client *client);
int client_disable_seat(struct client *client);

#endif
