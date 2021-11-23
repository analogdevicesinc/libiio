// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2018 Analog Devices, Inc.
 * Author: Robin Getz <robin.getz@analog.com>
 */

#include "iio-private.h"
#include <string.h>

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

int iio_channel_compare(const void *p1, const void *p2)
{
	const struct iio_channel *tmp1 = *(struct iio_channel **)p1;
	const struct iio_channel *tmp2 = *(struct iio_channel **)p2;

	/* make sure buffer enabled channels are first */
	if (iio_channel_is_scan_element(tmp1) && !iio_channel_is_scan_element(tmp2))
		return -1;
	if (!iio_channel_is_scan_element(tmp1) && iio_channel_is_scan_element(tmp2))
		return 1;
	/* and sort them by index */
	if (iio_channel_is_scan_element(tmp1) && iio_channel_is_scan_element(tmp2)){
		if (iio_channel_get_index(tmp1) > iio_channel_get_index(tmp2))
			return 1;
		return -1;
	}
	/* otherwise, if the ID is the same, input channels first */
	if (strcmp(tmp1->id, tmp2->id) == 0)
		return !iio_channel_is_output(tmp1);

	/* finally by ID */
	return strcmp(tmp1->id, tmp2->id);
}

int iio_channel_attr_compare(const void *p1, const void *p2)
{
	const struct iio_channel_attr *tmp1 = (struct iio_channel_attr *)p1;
	const struct iio_channel_attr *tmp2 = (struct iio_channel_attr *)p2;
	/* qsort channel attributes by name */
	return strcmp(tmp1->name, tmp2->name);
}

int iio_device_compare(const void *p1, const void *p2)
{
	const struct iio_device *tmp1 = *(struct iio_device **)p1;
	const struct iio_device *tmp2 = *(struct iio_device **)p2;
	/* qsort devices by ID */
	return strcmp(tmp1->id, tmp2->id);
}

int iio_device_attr_compare(const void *p1, const void *p2)
{
	const char *tmp1 = *(const char **)p1;
	const char *tmp2 = *(const char **)p2;
	/* qsort device attributes by name */
	return strcmp(tmp1, tmp2);
}

int iio_buffer_attr_compare(const void *p1, const void *p2)
{
	const char *tmp1 = *(const char **)p1;
	const char *tmp2 = *(const char **)p2;
	/* qsort buffer attributes by name */
	return strcmp(tmp1, tmp2);
}

int iio_context_info_compare(const void *p1, const void *p2)
{
	int ret;
	const struct iio_context_info *tmp1 = *(struct iio_context_info **)p1;
	const struct iio_context_info *tmp2 = *(struct iio_context_info **)p2;

	if(!tmp1->uri)
		return 1;
	if (!tmp2->uri)
		return 0;

	ret = strcmp(tmp1->uri, tmp2->uri);
	if (ret)
		return ret;

	return strcmp(tmp1->description, tmp2->description);
}
