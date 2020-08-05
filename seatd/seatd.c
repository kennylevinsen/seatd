#include <errno.h>
#include <poll.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include "client.h"
#include "log.h"
#include "poller.h"
#include "server.h"

int main(int argc, char *argv[]) {
	(void)argc;
	(void)argv;

	char *loglevel = getenv("SEATD_LOGLEVEL");
	enum log_level level = LOGLEVEL_ERROR;
	if (loglevel != NULL) {
		if (strcmp(loglevel, "silent") == 0) {
			level = LOGLEVEL_SILENT;
		} else if (strcmp(loglevel, "info") == 0) {
			level = LOGLEVEL_INFO;
		} else if (strcmp(loglevel, "debug") == 0) {
			level = LOGLEVEL_DEBUG;
		}
	}
	log_init(level);

	struct server server = {0};
	if (server_init(&server) == -1) {
		log_errorf("server_create failed: %s", strerror(errno));
		return 1;
	}
	char *path = getenv("SEATD_SOCK");
	if (path == NULL) {
		path = "/run/seatd.sock";
		struct stat st;
		if (stat(path, &st) == 0) {
			log_info("removing leftover seatd socket");
			unlink(path);
		}
	}

	if (server_listen(&server, path) == -1) {
		log_errorf("server_listen failed: %s", strerror(errno));
		server_finish(&server);
		return 1;
	}

	log_info("seatd started");

	while (server.running) {
		if (poller_poll(&server.poller) == -1) {
			log_errorf("poller failed: %s", strerror(errno));
			return 1;
		}
	}

	server_finish(&server);
	unlink(path);
	return 0;
}
