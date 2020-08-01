#ifndef _SEATD_EVDEV_H
#define _SEATD_EVDEV_H

int evdev_revoke(int fd);
int path_is_evdev(const char *path);

#if defined(__linux__)
#include <sys/types.h>
int dev_is_evdev(dev_t device);
#endif

#endif
