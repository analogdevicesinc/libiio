// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2023 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/ioctl.h>

#define IIO_FFS_DMABUF_ATTACH	_IOW('g', 131, int)
#define IIO_FFS_DMABUF_DETACH	_IOW('g', 132, int)
#define IIO_FFS_DMABUF_TRANSFER	_IOW('g', 133, struct iio_ffs_dmabuf_transfer)

struct iio_ffs_dmabuf_transfer {
	int fd;
	uint32_t flags;
	uint64_t length;
};

int usb_attach_dmabuf(int ep_fd, int fd)
{
	int ret;

	ret = ioctl(ep_fd, IIO_FFS_DMABUF_ATTACH, &fd);
	if (ret == -1)
		return -errno;

	return 0;
}

int usb_detach_dmabuf(int ep_fd, int fd)
{
	int ret;

	ret = ioctl(ep_fd, IIO_FFS_DMABUF_DETACH, &fd);
	if (ret == -1)
		return -errno;

	return 0;
}

int usb_transfer_dmabuf(int ep_fd, int fd, uint64_t size)
{
	struct iio_ffs_dmabuf_transfer req;
	int ret;

	req.fd = fd;
	req.length = size;
	req.flags = 0;

	ret = ioctl(ep_fd, IIO_FFS_DMABUF_TRANSFER, &req);
	if (ret == -1)
		return -errno;

	return 0;
}
