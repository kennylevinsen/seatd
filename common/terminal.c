#include "string.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#if defined(__linux__)
#include <linux/kd.h>
#include <linux/vt.h>
#define TTY0  "/dev/tty0"
#define TTYF  "/dev/tty%d"
#define K_ON  K_UNICODE
#define FRSIG 0
#elif defined(__FreeBSD__)
#include <sys/consio.h>
#include <sys/kbio.h>
#define TTY0 "/dev/ttyv0"
#define TTYF "/dev/ttyv%d"
#define K_ON  K_XLATE
#define K_OFF K_CODE
#define FRSIG SIGIO
#else
#error Unsupported platform
#endif

#include "log.h"
#include "terminal.h"

#define TTYPATHLEN 64

int terminal_current_vt(void) {
	int fd = open(TTY0, O_RDWR | O_NOCTTY);
	if (fd == -1) {
		log_errorf("could not open tty0: %s", strerror(errno));
		return -1;
	}

#if defined(__linux__)
	struct vt_stat st;
	int res = ioctl(fd, VT_GETSTATE, &st);
	close(fd);
	if (res == -1) {
		log_errorf("could not retrieve VT state: %s", strerror(errno));
		return -1;
	}
	return st.v_active;
#elif defined(__FreeBSD__)
	int vt;
	int res = ioctl(fd, VT_GETACTIVE, &vt);
	close(fd);
	if (res == -1) {
		log_errorf("could not retrieve VT state: %s", strerror(errno));
		return -1;
	}
	return vt;
#else
#error Unsupported platform
#endif
}

int terminal_setup(int vt) {
	log_debugf("setting up vt %d", vt);
	if (vt == -1) {
		vt = 0;
	}
	char path[TTYPATHLEN];
	if (snprintf(path, TTYPATHLEN, TTYF, vt) == -1) {
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
		.frsig = FRSIG,
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
	if (snprintf(path, TTYPATHLEN, TTYF, vt) == -1) {
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
	if (ioctl(fd, KDSKBMODE, K_ON) == -1) {
		log_errorf("could not set KD keyboard mode: %s", strerror(errno));
		close(fd);
		return -1;
	}

	static struct vt_mode mode = {
		.mode = VT_PROCESS,
		.waitv = 0,
		.relsig = SIGUSR1,
		.acqsig = SIGUSR2,
		.frsig = FRSIG,
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
	int fd = open(TTY0, O_RDWR | O_NOCTTY);
	if (fd == -1) {
		log_errorf("could not open tty0: %s", strerror(errno));
		return -1;
	}

	static struct vt_mode mode = {
		.mode = VT_PROCESS,
		.waitv = 0,
		.relsig = SIGUSR1,
		.acqsig = SIGUSR2,
		.frsig = FRSIG,
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
	int fd = open(TTY0, O_RDWR | O_NOCTTY);
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
	if (snprintf(path, TTYPATHLEN, TTYF, vt) == -1) {
		log_errorf("could not generate tty path: %s", strerror(errno));
		return -1;
	}
	int fd = open(path, O_RDWR | O_NOCTTY);
	if (fd == -1) {
		log_errorf("could not generate tty path: %s", strerror(errno));
		return -1;
	}
	int res = ioctl(fd, KDSKBMODE, enable ? K_OFF : K_OFF);
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
	if (snprintf(path, TTYPATHLEN, TTYF, vt) == -1) {
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
