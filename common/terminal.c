#include "string.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "log.h"
#include "terminal.h"

#define TTYPATHLEN 64

int terminal_current_vt(void) {
	struct vt_stat st;
	int fd = open("/dev/tty0", O_RDWR | O_NOCTTY);
	if (fd == -1) {
		log_errorf("could not open tty0: %s", strerror(errno));
		return -1;
	}
	int res = ioctl(fd, VT_GETSTATE, &st);
	close(fd);
	if (res == -1) {
		log_errorf("could not retrieve VT state: %s", strerror(errno));
		return -1;
	}
	return st.v_active;
}

int terminal_setup(int vt) {
	log_debugf("setting up vt %d", vt);
	if (vt == -1) {
		vt = 0;
	}
	char path[TTYPATHLEN];
	if (snprintf(path, TTYPATHLEN, "/dev/tty%d", vt) == -1) {
		log_errorf("could not generate tty path: %s", strerror(errno));
		return -1;
	}
	int fd = open(path, O_RDWR | O_NOCTTY);
	if (fd == -1) {
		log_errorf("could not open target tty: %s", strerror(errno));
		return -1;
	}

	static struct vt_mode mode = {
		.mode = VT_PROCESS,
		.waitv = 0,
		.relsig = SIGUSR1,
		.acqsig = SIGUSR2,
		.frsig = 0,
	};

	int res = ioctl(fd, VT_SETMODE, &mode);
	close(fd);
	if (res == -1) {
		log_errorf("could not set VT mode: %s", strerror(errno));
	}

	return res;
}

int terminal_teardown(int vt) {
	log_debugf("tearing down vt %d", vt);
	if (vt == -1) {
		vt = 0;
	}
	char path[TTYPATHLEN];
	if (snprintf(path, TTYPATHLEN, "/dev/tty%d", vt) == -1) {
		log_errorf("could not generate tty path: %s", strerror(errno));
		return -1;
	}
	int fd = open(path, O_RDWR | O_NOCTTY);
	if (fd == -1) {
		log_errorf("could not open target tty: %s", strerror(errno));
		return -1;
	}
	if (ioctl(fd, KDSETMODE, KD_TEXT) == -1) {
		log_errorf("could not set KD graphics mode: %s", strerror(errno));
		close(fd);
		return -1;
	}
	if (ioctl(fd, KDSKBMODE, K_UNICODE) == -1) {
		log_errorf("could not set KD keyboard mode: %s", strerror(errno));
		close(fd);
		return -1;
	}

	static struct vt_mode mode = {
		.mode = VT_PROCESS,
		.waitv = 0,
		.relsig = SIGUSR1,
		.acqsig = SIGUSR2,
		.frsig = 0,
	};
	if (ioctl(fd, VT_SETMODE, &mode) == -1) {
		log_errorf("could not set VT mode: %s", strerror(errno));
		close(fd);
		return -1;
	}

	close(fd);
	return 0;
}

int terminal_switch_vt(int vt) {
	log_debugf("switching to vt %d", vt);
	int fd = open("/dev/tty0", O_RDWR | O_NOCTTY);
	if (fd == -1) {
		log_errorf("could not open tty0: %s", strerror(errno));
		return -1;
	}

	static struct vt_mode mode = {
		.mode = VT_PROCESS,
		.waitv = 0,
		.relsig = SIGUSR1,
		.acqsig = SIGUSR2,
		.frsig = 0,
	};

	if (ioctl(fd, VT_SETMODE, &mode) == -1) {
		log_errorf("could not set VT mode: %s", strerror(errno));
		close(fd);
		return -1;
	}

	if (ioctl(fd, VT_ACTIVATE, vt) == -1) {
		log_errorf("could not activate VT: %s", strerror(errno));
		close(fd);
		return -1;
	}

	close(fd);
	return 0;
}

int terminal_ack_switch(void) {
	log_debug("acking vt switch");
	int fd = open("/dev/tty0", O_RDWR | O_NOCTTY);
	if (fd == -1) {
		log_errorf("could not open tty0: %s", strerror(errno));
		return -1;
	}

	int res = ioctl(fd, VT_RELDISP, VT_ACKACQ);
	close(fd);
	if (res == -1) {
		log_errorf("could not ack VT switch: %s", strerror(errno));
	}

	return res;
}

int terminal_set_keyboard(int vt, bool enable) {
	log_debugf("setting KD keyboard state to %d on vt %d", enable, vt);
	if (vt == -1) {
		vt = 0;
	}
	char path[TTYPATHLEN];
	if (snprintf(path, TTYPATHLEN, "/dev/tty%d", vt) == -1) {
		log_errorf("could not generate tty path: %s", strerror(errno));
		return -1;
	}
	int fd = open(path, O_RDWR | O_NOCTTY);
	if (fd == -1) {
		log_errorf("could not generate tty path: %s", strerror(errno));
		return -1;
	}
	int res = ioctl(fd, KDSKBMODE, enable ? K_UNICODE : K_OFF);
	close(fd);
	if (res == -1) {
		log_errorf("could not set KD keyboard mode: %s", strerror(errno));
	}
	return res;
}

int terminal_set_graphics(int vt, bool enable) {
	log_debugf("setting KD graphics state to %d on vt %d", enable, vt);
	if (vt == -1) {
		vt = 0;
	}
	char path[TTYPATHLEN];
	if (snprintf(path, TTYPATHLEN, "/dev/tty%d", vt) == -1) {
		log_errorf("could not generate tty path: %s", strerror(errno));
		return -1;
	}
	int fd = open(path, O_RDWR | O_NOCTTY);
	if (fd == -1) {
		log_errorf("could not generate tty path: %s", strerror(errno));
		return -1;
	}
	int res = ioctl(fd, KDSETMODE, enable ? KD_GRAPHICS : KD_TEXT);
	close(fd);
	if (res == -1) {
		log_errorf("could not set KD graphics mode: %s", strerror(errno));
	}
	return res;
}
