// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2015-2021 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "iio-config.h"
#include "iio-lock.h"

#include <stdlib.h>
#include <windows.h>

struct iio_mutex {
	CRITICAL_SECTION lock;
};

struct iio_cond {
	CONDITION_VARIABLE cond;
};

struct iio_mutex * iio_mutex_create(void)
{
	struct iio_mutex *lock = malloc(sizeof(*lock));

	if (!lock)
		return NULL;

	InitializeCriticalSection(&lock->lock);

	return lock;
}

void iio_mutex_destroy(struct iio_mutex *lock)
{
	DeleteCriticalSection(&lock->lock);
	free(lock);
}

void iio_mutex_lock(struct iio_mutex *lock)
{
	EnterCriticalSection(&lock->lock);
}

void iio_mutex_unlock(struct iio_mutex *lock)
{
	LeaveCriticalSection(&lock->lock);
}

struct iio_cond * iio_cond_create(void)
{
	struct iio_cond *cond = malloc(sizeof(*cond));

	if (!cond)
		return NULL;

	InitializeConditionVariable(&cond->cond);

	return cond;
}

void iio_cond_destroy(struct iio_cond *cond)
{
	free(cond);
}

void iio_cond_wait(struct iio_cond *cond, struct iio_mutex *lock)
{
	SleepConditionVariableCS(&cond->cond, &lock->lock, INFINITE);
}

void iio_cond_signal(struct iio_cond *cond)
{
	WakeConditionVariable(&cond->cond);
}
