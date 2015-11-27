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

#ifndef _IIO_LOCK_H
#define _IIO_LOCK_H

struct iio_mutex;

struct iio_mutex * iio_mutex_create(void);
void iio_mutex_destroy(struct iio_mutex *lock);

void iio_mutex_lock(struct iio_mutex *lock);
void iio_mutex_unlock(struct iio_mutex *lock);

#endif /* _IIO_LOCK_H */
