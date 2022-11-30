/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2022 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */
#ifndef __IIO_LOCAL_H
#define __IIO_LOCAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct iio_buffer_impl_pdata;
struct iio_block_impl_pdata;
struct iio_device;
struct timespec;

struct iio_buffer_pdata {
	const struct iio_device *dev;
	struct iio_buffer_impl_pdata *pdata;
	int fd, cancel_fd;
	unsigned int idx;
	bool multi_buffer;
	bool dmabuf_supported;
	bool mmap_supported;
	size_t size;
};

struct iio_block_pdata {
	struct iio_buffer_pdata *buf;
	struct iio_block_impl_pdata *pdata;
	size_t size;
	void *data;
	bool dequeued;
	bool cpu_access_disabled;
};

int ioctl_nointr(int fd, unsigned long request, void *data);

int buffer_check_ready(struct iio_buffer_pdata *pdata, int fd,
		       short events, struct timespec *start);

struct iio_block_pdata *
local_create_dmabuf(struct iio_buffer_pdata *pdata, size_t size, void **data);
void local_free_dmabuf(struct iio_block_pdata *pdata);

int local_enqueue_dmabuf(struct iio_block_pdata *pdata,
			 size_t bytes_used, bool cyclic);
int local_dequeue_dmabuf(struct iio_block_pdata *pdata, bool nonblock);

int local_dmabuf_disable_cpu_access(struct iio_block_pdata *pdata, bool disable);

struct iio_block_pdata *
local_create_mmap_block(struct iio_buffer_pdata *pdata,
			size_t size, void **data);
void local_free_mmap_block(struct iio_block_pdata *pdata);

int local_enqueue_mmap_block(struct iio_block_pdata *pdata,
			     size_t bytes_used, bool cyclic);
int local_dequeue_mmap_block(struct iio_block_pdata *pdata, bool nonblock);

struct iio_buffer_impl_pdata * local_alloc_mmap_buffer_impl(void);

#endif /* __IIO_LOCAL_H */
