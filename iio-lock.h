/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2015 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#ifndef _IIO_LOCK_H
#define _IIO_LOCK_H

#include <iio.h>

#define __api __iio_api

struct iio_mutex;
struct iio_cond;

__api struct iio_mutex *iio_mutex_create(void);
__api void iio_mutex_destroy(struct iio_mutex *lock);

__api void iio_mutex_lock(struct iio_mutex *lock);
__api void iio_mutex_unlock(struct iio_mutex *lock);

__api struct iio_cond * iio_cond_create(void);
__api void iio_cond_destroy(struct iio_cond *cond);

__api void iio_cond_wait(struct iio_cond *cond, struct iio_mutex *lock);
__api void iio_cond_signal(struct iio_cond *cond);

#undef __api

#endif /* _IIO_LOCK_H */
