#ifndef _SEATD_SERVER_H
#define _SEATD_SERVER_H

#include <stdbool.h>

#include "list.h"

struct poller;
struct client;

struct server {
	bool running;
	struct poller *poller;

	struct list seats;
};

struct server *server_create(void);
void server_destroy(struct server *server);

struct seat *server_get_seat(struct server *server, const char *seat_name);

int server_listen(struct server *server, const char *path);
int server_add_client(struct server *server, int fd);

#endif
