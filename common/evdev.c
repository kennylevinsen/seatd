#include <linux/input.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>
#include <sys/types.h>

#include "evdev.h"

int evdev_revoke(int fd) {
	return ioctl(fd, EVIOCREVOKE, NULL);
}

int dev_is_evdev(dev_t device) {
	return major(device) == 13;
}
