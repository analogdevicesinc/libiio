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

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

#include <stdlib.h>

struct iio_mutex {
#ifdef _WIN32
	CRITICAL_SECTION lock;
#else
	pthread_mutex_t lock;
#endif
};

struct iio_mutex * iio_mutex_create(void)
{
	struct iio_mutex *lock = malloc(sizeof(*lock));

	if (!lock)
		return NULL;

#ifdef _WIN32
	InitializeCriticalSection(&lock->lock);
#else
	pthread_mutex_init(&lock->lock, NULL);
#endif
	return lock;
}

void iio_mutex_destroy(struct iio_mutex *lock)
{
#ifdef _WIN32
	DeleteCriticalSection(&lock->lock);
#else
	pthread_mutex_destroy(&lock->lock);
#endif
	free(lock);
}

void iio_mutex_lock(struct iio_mutex *lock)
{
#ifdef _WIN32
	EnterCriticalSection(&lock->lock);
#else
	pthread_mutex_lock(&lock->lock);
#endif
}

void iio_mutex_unlock(struct iio_mutex *lock)
{
#ifdef _WIN32
	LeaveCriticalSection(&lock->lock);
#else
	pthread_mutex_unlock(&lock->lock);
#endif
}
