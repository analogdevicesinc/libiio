/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2018 Analog Devices, Inc.
 * Author: Robin Getz <robin.getz@analog.com>
 */

#ifndef __IIO_QSORT_H__
#define __IIO_QSORT_H__

int iio_channel_compare(const void *p1, const void *p2);
int iio_channel_attr_compare(const void *p1, const void *p2);
int iio_device_compare(const void *p1, const void *p2);
int iio_device_attr_compare(const void *p1, const void *p2);
int iio_buffer_attr_compare(const void *p1, const void *p2);
int iio_context_info_compare(const void *p1, const void *p2);

#endif /* __IIO_QSORT_H__ */
