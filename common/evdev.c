#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#if defined(__linux__)
#include <linux/input.h>
#include <sys/sysmacros.h>
#elif defined(__FreeBSD__)
#include <dev/evdev/input.h>
#else
#error Unsupported platform
#endif

#include "evdev.h"

int evdev_revoke(int fd) {
	return ioctl(fd, EVIOCREVOKE, NULL);
}

int dev_is_evdev(dev_t device) {
	return major(device) == 13;
}
