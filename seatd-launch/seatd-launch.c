#include <errno.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
	(void)argc;
	char sockbuf[256];

	sprintf(sockbuf, "/tmp/seatd.%d.sock", getpid());
	unlink(sockbuf);

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

		char pipebuf[8];
		sprintf(pipebuf, "%d", fds[1]);

		struct passwd *user = getpwuid(getuid());
		if (!user) {
			perror("getpwuid failed");
			_exit(1);
		}

		// TODO: Make seatd accept the numeric UID
		execlp("seatd", "seatd", "-n", pipebuf, "-u", user->pw_name, "-s", sockbuf, NULL);
		perror("Could not start seatd");
		_exit(1);
	}
	close(fds[1]);

	// Drop privileges
	if (setgid(getgid()) == -1) {
		perror("Could not set gid to drop privileges");
		goto error_seatd;
	}
	if (setuid(getuid()) == -1) {
		perror("Could not set uid to drop privileges");
		goto error_seatd;
	}

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

	pid_t child = fork();
	if (child == -1) {
		perror("Could not fork target process");
		goto error_seatd;
	} else if (child == 0) {
		setenv("SEATD_SOCK", sockbuf, 1);
		execv(argv[1], &argv[1]);
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

	unlink(sockbuf);
	kill(seatd_child, SIGTERM);

	if (WIFEXITED(status)) {
		return WEXITSTATUS(status);
	} else {
		return 1;
	}

error_seatd:
	unlink(sockbuf);
	kill(seatd_child, SIGTERM);
error:
	return 1;
}
