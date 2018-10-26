/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2018 Analog Devices, Inc.
 * Author: Robin Getz <robin.getz@analog.com>
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
 * */

#ifndef __IIO_QSORT_H__
#define __IIO_QSORT_H__

int iio_channel_compare(const void *p1, const void *p2);
int iio_channel_attr_compare(const void *p1, const void *p2);
int iio_device_compare(const void *p1, const void *p2);
int iio_device_attr_compare(const void *p1, const void *p2);
int iio_buffer_attr_compare(const void *p1, const void *p2);

#endif /* __IIO_QSORT_H__ */
