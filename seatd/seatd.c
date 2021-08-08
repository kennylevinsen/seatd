#include <errno.h>
#include <grp.h>
#include <poll.h>
#include <pwd.h>
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

#define LISTEN_BACKLOG 16

static int open_socket(const char *path, int uid, int gid) {
	union {
		struct sockaddr_un unix;
		struct sockaddr generic;
	} addr = {{0}};
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (fd == -1) {
		log_errorf("Could not create socket: %s", strerror(errno));
		return -1;
	}

	addr.unix.sun_family = AF_UNIX;
	strncpy(addr.unix.sun_path, path, sizeof addr.unix.sun_path - 1);
	socklen_t size = offsetof(struct sockaddr_un, sun_path) + strlen(addr.unix.sun_path);
	if (bind(fd, &addr.generic, size) == -1) {
		log_errorf("Could not bind socket: %s", strerror(errno));
		goto error;
	}
	if (listen(fd, LISTEN_BACKLOG) == -1) {
		log_errorf("Could not listen on socket: %s", strerror(errno));
		goto error;
	}
	if (uid != -1 || gid != -1) {
		if (fchown(fd, uid, gid) == -1) {
			log_errorf("Could not chown socket to uid %d, gid %d: %s", uid, gid,
				   strerror(errno));
			goto error;
		}
		if (fchmod(fd, 0770) == -1) {
			log_errorf("Could not chmod socket: %s", strerror(errno));
			goto error;
		}
	}
	return fd;
error:
	close(fd);
	return -1;
}

int main(int argc, char *argv[]) {
	char *loglevel = getenv("SEATD_LOGLEVEL");
	enum libseat_log_level level = LIBSEAT_LOG_LEVEL_ERROR;
	if (loglevel != NULL) {
		if (strcmp(loglevel, "silent") == 0) {
			level = LIBSEAT_LOG_LEVEL_SILENT;
		} else if (strcmp(loglevel, "info") == 0) {
			level = LIBSEAT_LOG_LEVEL_INFO;
		} else if (strcmp(loglevel, "debug") == 0) {
			level = LIBSEAT_LOG_LEVEL_DEBUG;
		}
	}
	log_init();
	libseat_set_log_level(level);

	const char *usage = "Usage: seatd [options]\n"
			    "\n"
			    "  -h		Show this help message\n"
			    "  -n <fd>          FD to notify readiness on\n"
			    "  -u <user>	User to own the seatd socket\n"
			    "  -g <group>	Group to own the seatd socket\n"
			    "  -s <path>	Where to create the seatd socket\n"
			    "  -v		Show the version number\n"
			    "\n";

	int c;
	int uid = -1, gid = -1;
	int readiness = -1;
	const char *socket_path = getenv("SEATD_SOCK");
	while ((c = getopt(argc, argv, "vhn:s:g:u:")) != -1) {
		switch (c) {
		case 'n':
			readiness = atoi(optarg);
			if (readiness < 0) {
				fprintf(stderr, "Invalid readiness fd: %s\n", optarg);
				return 1;
			}
			break;
		case 's':
			socket_path = optarg;
			break;
		case 'u': {
			struct passwd *pw = getpwnam(optarg);
			if (pw == NULL) {
				fprintf(stderr, "Could not find user by name '%s'.\n", optarg);
				return 1;
			} else {
				uid = pw->pw_uid;
			}
			break;
		}
		case 'g': {
			struct group *gr = getgrnam(optarg);
			if (gr == NULL) {
				fprintf(stderr, "Could not find group by name '%s'.\n", optarg);
				return 1;
			} else {
				gid = gr->gr_gid;
			}
			break;
		}
		case 'v':
			printf("seatd version %s\n", SEATD_VERSION);
			return 0;
		case 'h':
			printf("%s", usage);
			return 0;
		case '?':
			fprintf(stderr, "Try '%s -h' for more information.\n", argv[0]);
			return 1;
		default:
			abort();
		}
	}

	if (socket_path == NULL) {
		socket_path = SEATD_DEFAULTPATH;
		struct stat st;
		if (stat(socket_path, &st) == 0) {
			log_info("Removing leftover seatd socket");
			unlink(socket_path);
		}
	}

	struct server server = {0};
	if (server_init(&server) == -1) {
		log_errorf("server_create failed: %s", strerror(errno));
		return 1;
	}

	int socket_fd = open_socket(socket_path, uid, gid);
	if (socket_fd == -1) {
		log_errorf("Could not create server socket: %s", strerror(errno));
		server_finish(&server);
		return 1;
	}
	if (poller_add_fd(&server.poller, socket_fd, EVENT_READABLE, server_handle_connection,
			  &server) == NULL) {
		log_errorf("Could not add socket to poller: %s", strerror(errno));
		close(socket_fd);
		server_finish(&server);
		return 1;
	}

	log_info("seatd started");

	if (readiness != -1) {
		if (write(readiness, "\n", 1) == -1) {
			log_errorf("Could not write readiness signal: %s\n", strerror(errno));
		}
		close(readiness);
	}

	while (server.running) {
		if (poller_poll(&server.poller) == -1) {
			log_errorf("Poller failed: %s", strerror(errno));
			return 1;
		}
	}

	server_finish(&server);
	unlink(socket_path);
	log_info("seatd stopped");
	return 0;
}
