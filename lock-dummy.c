// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2024 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include <iio/iio.h>
#include <iio/iio-lock.h>

#include <errno.h>
#include <stdlib.h>

struct iio_mutex {
	int dummy; /* Flawfinder: ignore */
};

struct iio_cond {
	int dummy; /* Flawfinder: ignore */
};

struct iio_mutex * iio_mutex_create(void)
{
	struct iio_mutex *lock = malloc(sizeof(*lock));

	if (!lock)
		return iio_ptr(-ENOMEM);

	return lock;
}

void iio_mutex_destroy(struct iio_mutex *lock)
{
	free(lock);
}

void iio_mutex_lock(struct iio_mutex *lock)
{
}

void iio_mutex_unlock(struct iio_mutex *lock)
{
}

struct iio_cond * iio_cond_create(void)
{
	struct iio_cond *cond = malloc(sizeof(*cond));

	if (!cond)
		return iio_ptr(-ENOMEM);

	return cond;
}

void iio_cond_destroy(struct iio_cond *cond)
{
	free(cond);
}

int iio_cond_wait(struct iio_cond *cond, struct iio_mutex *lock,
		  unsigned int timeout_ms)
{
	return -ETIMEDOUT;
}

void iio_cond_signal(struct iio_cond *cond)
{
}

struct iio_thrd * iio_thrd_create(int (*thrd)(void *),
				  void *d, const char *name)
{
	return iio_ptr(-ENOSYS);
}

int iio_thrd_join_and_destroy(struct iio_thrd *thrd)
{
	return 0;
}
