/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2023 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#ifndef __IIO_ATTR_H__
#define __IIO_ATTR_H__

#include <iio/iio-backend.h>

struct iio_attr_list;
struct iio_context;
struct iio_device;

const struct iio_attr *
iio_attr_get(const struct iio_attr_list *attrs, unsigned int idx);
const struct iio_attr *
iio_attr_find(const struct iio_attr_list *attrs, const char *name);

void iio_free_attr_data(struct iio_attr *attr);
void iio_free_attrs(const struct iio_attr_list *attrs);

int iio_add_attr(union iio_pointer p, struct iio_attr_list *attrs,
		 const char *name, const char *filename,
		 enum iio_attr_type type);

int iio_context_add_attr(struct iio_context *ctx,
			 const char *key, const char *value);
int iio_device_add_attr(struct iio_device *dev,
			const char *name, enum iio_attr_type type);
int iio_channel_add_attr(struct iio_channel *chn,
			 const char *name, const char *filename);

#endif /* __IIO_ATTR_H__ */
