/* SPDX-License-Identifier: MIT */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2018 Analog Devices, Inc.
 * Author: Robin Getz <robin.getz@analog.com>
 */

#ifndef __IIO_QSORT_H__
#define __IIO_QSORT_H__

struct iio_attr_list;
struct iio_buffer;
struct iio_context;
struct iio_device;

void iio_sort_attrs(struct iio_attr_list *attrs);
void iio_sort_devices(struct iio_context *ctx);
void iio_sort_channels(struct iio_device *dev);
void iio_sort_scan_elements(struct iio_buffer *buf);

#endif /* __IIO_QSORT_H__ */
