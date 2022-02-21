#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
	(void)argc;

	const char *usage = "Usage: seatd-launch [options] [--] command\n"
			    "\n"
			    "  -h		Show this help message\n"
			    "  -s <path>	Where to create the seatd socket\n"
			    "  -v		Show the version number\n"
			    "\n";

	int c;
	char *sockpath = NULL;
	while ((c = getopt(argc, argv, "vhs:")) != -1) {
		switch (c) {
		case 's':
			sockpath = optarg;
			break;
		case 'v':
			printf("seatd-launch version %s\n", SEATD_VERSION);
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

	if (optind >= argc) {
		fprintf(stderr, "A command must be specified\n\n%s", usage);
		return 1;
	}
	char **command = &argv[optind];

	char sockbuf[256];
	if (sockpath == NULL) {
		sprintf(sockbuf, "/tmp/seatd.%d.sock", getpid());
		sockpath = sockbuf;
	}

	int fds[2];
	if (pipe(fds) == -1) {
		perror("Could not create pipe");
		goto error;
	}

	pid_t seatd_child = fork();
	if (seatd_child == -1) {
		perror("Could not fork seatd process");
		goto error;
	} else if (seatd_child == 0) {
		close(fds[0]);

		char pipebuf[16] = {0};
		snprintf(pipebuf, sizeof pipebuf, "%d", fds[1]);

		char *env[2] = {NULL, NULL};
		char loglevelbuf[32] = {0};
		char *cur_loglevel = getenv("SEATD_LOGLEVEL");
		if (cur_loglevel != NULL) {
			snprintf(loglevelbuf, sizeof loglevelbuf, "SEATD_LOGLEVEL=%s", cur_loglevel);
			env[0] = loglevelbuf;
		}

		char *command[] = {"seatd", "-n", pipebuf, "-s", sockpath, NULL};
		execve(SEATD_INSTALLPATH, command, env);
		perror("Could not start seatd");
		_exit(1);
	}
	close(fds[1]);

	// Wait for seatd to be ready
	char buf[1] = {0};
	while (true) {
		pid_t p = waitpid(seatd_child, NULL, WNOHANG);
		if (p == seatd_child) {
			fprintf(stderr, "seatd exited prematurely\n");
			goto error_seatd;
		} else if (p == -1 && (errno != EINTR && errno != ECHILD)) {
			perror("Could not wait for seatd process");
			goto error_seatd;
		}

		struct pollfd fd = {
			.fd = fds[0],
			.events = POLLIN,
		};

		// We poll with timeout to avoid a racing on a blocking read
		if (poll(&fd, 1, 1000) == -1) {
			if (errno == EAGAIN || errno == EINTR) {
				continue;
			} else {
				perror("Could not poll notification fd");
				goto error_seatd;
			}
		}

		if (fd.revents & POLLIN) {
			ssize_t n = read(fds[0], buf, 1);
			if (n == -1 && errno != EINTR) {
				perror("Could not read from pipe");
				goto error_seatd;
			} else if (n > 0) {
				break;
			}
		}
	}
	close(fds[0]);

	uid_t uid = getuid();
	gid_t gid = getgid();

	// Restrict access to the socket to just us
	if (chown(sockpath, uid, gid) == -1) {
		perror("Could not chown seatd socket");
		goto error_seatd;
	}
	if (chmod(sockpath, 0700) == -1) {
		perror("Could not chmod socket");
		goto error_seatd;
	}

	// Drop privileges
	if (setgid(gid) == -1) {
		perror("Could not set gid to drop privileges");
		goto error_seatd;
	}
	if (setuid(uid) == -1) {
		perror("Could not set uid to drop privileges");
		goto error_seatd;
	}

	pid_t child = fork();
	if (child == -1) {
		perror("Could not fork target process");
		goto error_seatd;
	} else if (child == 0) {
		setenv("SEATD_SOCK", sockpath, 1);
		execvp(command[0], command);
		perror("Could not start target");
		_exit(1);
	}

	int status = 0;
	while (true) {
		pid_t p = waitpid(child, &status, 0);
		if (p == child) {
			break;
		} else if (p == -1 && errno != EINTR) {
			perror("Could not wait for target process");
			goto error_seatd;
		}
	}

	if (kill(seatd_child, SIGTERM) != 0) {
		perror("Could not kill seatd");
	}

	if (WIFEXITED(status)) {
		return WEXITSTATUS(status);
	} else if (WIFSIGNALED(status)) {
		return 128 + WTERMSIG(status);
	} else {
		abort(); // unreachable
	}

error_seatd:
	kill(seatd_child, SIGTERM);
error:
	return 1;
}
