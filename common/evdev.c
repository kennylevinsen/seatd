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
#elif defined(__NetBSD__)
#include <stdlib.h>
#include <sys/stat.h>
#else
#error Unsupported platform
#endif

#include "evdev.h"

#define STRLEN(s) ((sizeof(s) / sizeof(s[0])) - 1)

#if defined(__linux__) || defined(__FreeBSD__)
int path_is_evdev(const char *path) {
	static const char prefix[] = "/dev/input/event";
	static const size_t prefixlen = STRLEN(prefix);
	return strncmp(prefix, path, prefixlen) == 0;
}

int evdev_revoke(int fd) {
	return ioctl(fd, EVIOCREVOKE, NULL);
}
#endif

#if defined(__linux__)
int dev_is_evdev(dev_t device) {
	return major(device) == INPUT_MAJOR;
}
#elif defined(__NetBSD__)
int dev_is_evdev(dev_t device) {
	return major(device) == getdevmajor("wskbd", S_IFCHR) ||
	       major(device) == getdevmajor("wsmouse", S_IFCHR) ||
	       major(device) == getdevmajor("wsmux", S_IFCHR);
}
int path_is_evdev(const char *path) {
	const char *wskbd = "/dev/wskbd";
	const char *wsmouse = "/dev/wsmouse";
	const char *wsmux = "/dev/wsmux";
	return strncmp(path, wskbd, STRLEN(wskbd)) == 0 ||
	       strncmp(path, wsmouse, STRLEN(wsmouse)) == 0 ||
	       strncmp(path, wsmux, STRLEN(wsmouse)) == 0;
}
int evdev_revoke(int fd) {
	(void)fd;
	return 0;
}
#endif
