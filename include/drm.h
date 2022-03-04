#ifndef _SEATD_DRM_H
#define _SEATD_DRM_H

int drm_set_master(int fd);
int drm_drop_master(int fd);
int path_is_drm(const char *path);

#if defined(__linux__) || defined(__NetBSD__)
#include <sys/types.h>
int dev_is_drm(dev_t device);
#endif

#endif
