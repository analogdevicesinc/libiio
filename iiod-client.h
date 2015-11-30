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
			int desc, const char *src, size_t len);
	ssize_t (*read)(struct iio_context_pdata *pdata,
			int desc, char *dst, size_t len);
	ssize_t (*read_line)(struct iio_context_pdata *pdata,
			int desc, char *dst, size_t len);
};

struct iiod_client * iiod_client_new(struct iio_context_pdata *pdata,
		struct iio_mutex *lock, const struct iiod_client_ops *ops);
void iiod_client_destroy(struct iiod_client *client);

int iiod_client_get_version(struct iiod_client *client, int desc,
		unsigned int *major, unsigned int *minor, char *git_tag);
int iiod_client_get_trigger(struct iiod_client *client, int desc,
		const struct iio_device *dev,
		const struct iio_device **trigger);
int iiod_client_set_trigger(struct iiod_client *client, int desc,
		const struct iio_device *dev, const struct iio_device *trigger);

#endif /* _IIOD_CLIENT_H */
