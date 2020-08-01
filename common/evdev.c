#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#if defined(__linux__)
#include <linux/input.h>
#include <linux/major.h>
#include <sys/sysmacros.h>
#elif defined(__FreeBSD__)
#include <dev/evdev/input.h>
#else
#error Unsupported platform
#endif

#include "compiler.h"
#include "evdev.h"

int path_is_evdev(const char *path) {
	static const char prefix[] = "/dev/input/event";
	static const size_t prefixlen = STRLEN(prefix);
	return strncmp(prefix, path, prefixlen) == 0;
}

int evdev_revoke(int fd) {
	return ioctl(fd, EVIOCREVOKE, NULL);
}

#if defined(__linux__)
int dev_is_evdev(dev_t device) {
	return major(device) == INPUT_MAJOR;
}
#endif
