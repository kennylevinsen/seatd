#ifndef _SEATD_DRM_H
#define _SEATD_DRM_H

#include <sys/types.h>

int drm_set_master(int fd);
int drm_drop_master(int fd);
int dev_is_drm(dev_t device);

#endif
