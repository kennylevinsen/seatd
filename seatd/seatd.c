#include <errno.h>
#include <poll.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "client.h"
#include "log.h"
#include "poller.h"
#include "server.h"

int main(int argc, char *argv[]) {
	(void)argc;
	(void)argv;

	char *loglevel = getenv("SEATD_LOGLEVEL");
	enum libseat_log_level level = LIBSEAT_ERROR;
	if (loglevel != NULL) {
		if (strcmp(loglevel, "silent") == 0) {
			level = LIBSEAT_SILENT;
		} else if (strcmp(loglevel, "info") == 0) {
			level = LIBSEAT_INFO;
		} else if (strcmp(loglevel, "debug") == 0) {
			level = LIBSEAT_DEBUG;
		}
	}
	libseat_log_init(level);

	struct server *server = server_create();
	if (server == NULL) {
		log_errorf("server_create failed: %s", strerror(errno));
		return 1;
	}
	char *path = getenv("SEATD_SOCK");
	if (path == NULL) {
		path = "/run/seatd.sock";
	}

	if (server_listen(server, path) == -1) {
		log_errorf("server_listen failed: %s", strerror(errno));
		server_destroy(server);
		return 1;
	}

	log_info("seatd started");

	while (server->running) {
		if (poller_poll(server->poller) == -1) {
			log_errorf("poller failed: %s", strerror(errno));
			return 1;
		}
	}

	server_destroy(server);
	unlink(path);
	return 0;
}
