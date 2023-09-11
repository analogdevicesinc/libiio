// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2018 Analog Devices, Inc.
 * Author: Robin Getz <robin.getz@analog.com>
 */

#include "iio-private.h"
#include <string.h>
#include <stdlib.h>

/* These are a few functions to do sorting via qsort for various
 * iio structures. For more info, see the qsort(3) man page.
 *
 * The qsort comparison function must return an integer less than, equal to,
 * or greater than zero if the first argument is considered to be
 * respectively less than, equal to, or greater than the second. If two
 * members compare as equal, their order in the sort order is undefined.
 *
 * If the structures are updated, the compare functions may
 * need to be updated.
 *
 * The actual arguments to these function are "pointers to
 * pointers to char", but strcmp(3) arguments are "pointers
 * to char", hence the cast plus dereference
 */

static int iio_channel_compare(const void *p1, const void *p2)
{
	const struct iio_channel *tmp1 = *(struct iio_channel **)p1;
	const struct iio_channel *tmp2 = *(struct iio_channel **)p2;
	long idx1 = iio_channel_get_index(tmp1);
	long idx2 = iio_channel_get_index(tmp2);

	if (idx1 == idx2 && idx1 >= 0) {
		idx1 = iio_channel_get_data_format(tmp1)->shift;
		idx2 = iio_channel_get_data_format(tmp2)->shift;
	}

	if (idx2 >= 0 && (idx1 > idx2 || idx1 < 0))
		return 1;

	return -1;
}

static int iio_device_compare(const void *p1, const void *p2)
{
	const struct iio_device *tmp1 = *(struct iio_device **)p1;
	const struct iio_device *tmp2 = *(struct iio_device **)p2;
	/* qsort devices by ID */
	return strcmp(tmp1->id, tmp2->id);
}

static int iio_attr_compare(const void *p1, const void *p2)
{
	const struct iio_attr *attr1 = (const struct iio_attr *)p1;
	const struct iio_attr *attr2 = (const struct iio_attr *)p2;

	return strcmp(attr1->name, attr2->name);
}

void iio_sort_attrs(struct iio_attr_list *attrs)
{
	if (attrs->num > 0) {
		qsort(attrs->attrs, attrs->num,
		      sizeof(*attrs->attrs), iio_attr_compare);
	}
}

void iio_sort_devices(struct iio_context *ctx)
{
	qsort(ctx->devices, ctx->nb_devices,
	      sizeof(*ctx->devices), iio_device_compare);
}

void iio_sort_channels(struct iio_device *dev)
{
	qsort(dev->channels, dev->nb_channels,
	      sizeof(*dev->channels), iio_channel_compare);
}
