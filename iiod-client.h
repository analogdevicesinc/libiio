/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2015 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 */

#ifndef _IIOD_CLIENT_H
#define _IIOD_CLIENT_H

#include "iio.h"

struct iio_mutex;
struct iiod_client;
struct iio_context_pdata;

struct iiod_client_ops {
	ssize_t (*write)(struct iio_context_pdata *pdata,
			void *desc, const char *src, size_t len);
	ssize_t (*read)(struct iio_context_pdata *pdata,
			void *desc, char *dst, size_t len);
	ssize_t (*read_line)(struct iio_context_pdata *pdata,
			void *desc, char *dst, size_t len);
};

struct iiod_client * iiod_client_new(struct iio_context_pdata *pdata,
		struct iio_mutex *lock, const struct iiod_client_ops *ops);
void iiod_client_destroy(struct iiod_client *client);

int iiod_client_get_version(struct iiod_client *client, void *desc,
		unsigned int *major, unsigned int *minor, char *git_tag);
int iiod_client_get_trigger(struct iiod_client *client, void *desc,
		const struct iio_device *dev,
		const struct iio_device **trigger);
int iiod_client_set_trigger(struct iiod_client *client, void *desc,
		const struct iio_device *dev, const struct iio_device *trigger);
int iiod_client_set_kernel_buffers_count(struct iiod_client *client,
		void *desc, const struct iio_device *dev, unsigned int nb_blocks);
int iiod_client_set_timeout(struct iiod_client *client,
		void *desc, unsigned int timeout);
ssize_t iiod_client_read_attr(struct iiod_client *client, void *desc,
		const struct iio_device *dev, const struct iio_channel *chn,
		const char *attr, char *dest, size_t len, bool is_debug);
ssize_t iiod_client_write_attr(struct iiod_client *client, void *desc,
		const struct iio_device *dev, const struct iio_channel *chn,
		const char *attr, const char *src, size_t len, bool is_debug);
int iiod_client_open_unlocked(struct iiod_client *client, void *desc,
		const struct iio_device *dev, size_t samples_count,
		bool cyclic);
int iiod_client_close_unlocked(struct iiod_client *client, void *desc,
		const struct iio_device *dev);
ssize_t iiod_client_read_unlocked(struct iiod_client *client, void *desc,
		const struct iio_device *dev, void *dst, size_t len,
		uint32_t *mask, size_t words);
ssize_t iiod_client_write_unlocked(struct iiod_client *client, void *desc,
		const struct iio_device *dev, const void *src, size_t len);
struct iio_context * iiod_client_create_context(
		struct iiod_client *client, void *desc);

#endif /* _IIOD_CLIENT_H */
