#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#if defined(__linux__)
#include <linux/kd.h>
#include <linux/vt.h>
#define TTYF	  "/dev/tty%d"
#define K_ENABLE  K_UNICODE
#define K_DISABLE K_OFF
#define FRSIG	  0
#elif defined(__FreeBSD__)
#include <sys/consio.h>
#include <sys/kbio.h>
#include <termios.h>
#define TTYF	  "/dev/ttyv%d"
#define K_ENABLE  K_XLATE
#define K_DISABLE K_RAW
#define FRSIG	  SIGIO
#else
#error Unsupported platform
#endif

#include "log.h"
#include "terminal.h"

#define TTYPATHLEN 64

int terminal_open(int vt) {
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
	return fd;
}

int terminal_current_vt(int fd) {
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

int terminal_set_process_switching(int fd, bool enable) {
	log_debug("setting process switching");
	struct vt_mode mode = {
		.mode = enable ? VT_PROCESS : VT_AUTO,
		.waitv = 0,
		.relsig = enable ? SIGUSR1 : 0,
		.acqsig = enable ? SIGUSR2 : 0,
		.frsig = FRSIG,
	};

	if (ioctl(fd, VT_SETMODE, &mode) == -1) {
		log_errorf("could not set VT mode: %s", strerror(errno));
		return -1;
	}
	return 0;
}

int terminal_switch_vt(int fd, int vt) {
	log_debugf("switching to vt %d", vt);
	if (ioctl(fd, VT_ACTIVATE, vt) == -1) {
		log_errorf("could not activate VT: %s", strerror(errno));
		return -1;
	}

	return 0;
}

int terminal_ack_switch(int fd) {
	log_debug("acking vt switch");
	if (ioctl(fd, VT_RELDISP, VT_ACKACQ) == -1) {
		log_errorf("could not ack VT switch: %s", strerror(errno));
		return -1;
	}

	return 0;
}

int terminal_set_keyboard(int fd, bool enable) {
	log_debugf("setting KD keyboard state to %d", enable);
	if (ioctl(fd, KDSKBMODE, enable ? K_ENABLE : K_DISABLE) == -1) {
		log_errorf("could not set KD keyboard mode: %s", strerror(errno));
		return -1;
	}
#if defined(__FreeBSD__)
	struct termios tios;
	if (tcgetattr(fd, &tios) == -1) {
		log_errorf("could not set get terminal mode: %s", strerror(errno));
		return -1;
	}
	if (enable) {
		cfmakesane(&tios);
	} else {
		cfmakeraw(&tios);
	}
	if (tcsetattr(fd, TCSAFLUSH, &tios) == -1) {
		log_errorf("could not set terminal mode: %s", strerror(errno));
		return -1;
	}
#endif
	return 0;
}

int terminal_set_graphics(int fd, bool enable) {
	log_debugf("setting KD graphics state to %d", enable);
	if (ioctl(fd, KDSETMODE, enable ? KD_GRAPHICS : KD_TEXT) == -1) {
		log_errorf("could not set KD graphics mode: %s", strerror(errno));
		return -1;
	}
	return 0;
}
