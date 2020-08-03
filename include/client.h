#ifndef _SEATD_CLIENT_H
#define _SEATD_CLIENT_H

#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>

#include "connection.h"
#include "linked_list.h"

struct server;

struct client {
	struct linked_list link; // seat::clients
	struct server *server;
	struct event_source_fd *event_source;
	struct connection connection;

	pid_t pid;
	uid_t uid;
	gid_t gid;

	struct seat *seat;
	int seat_vt;
	bool pending_disable;

	struct linked_list devices;
};

struct client *client_create(struct server *server, int client_fd);
void client_kill(struct client *client);
void client_destroy(struct client *client);

int client_handle_connection(int fd, uint32_t mask, void *data);
int client_get_session(const struct client *client);
int client_send_enable_seat(struct client *client);
int client_send_disable_seat(struct client *client);

#endif
