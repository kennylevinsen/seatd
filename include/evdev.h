#ifndef _SEATD_EVDEV_H
#define _SEATD_EVDEV_H

#include <sys/types.h>

int evdev_revoke(int fd);
int dev_is_evdev(dev_t device);

#endif
