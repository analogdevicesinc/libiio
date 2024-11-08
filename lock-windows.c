// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2015-2021 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "iio-config.h"

#include <iio/iio-backend.h>
#include <iio/iio-lock.h>

#include <errno.h>
#include <stdlib.h>
#include <windows.h>

struct iio_mutex {
	CRITICAL_SECTION lock;
};

struct iio_cond {
	CONDITION_VARIABLE cond;
};

struct iio_thrd {
	HANDLE thid;

	void *d;
	int (*func)(void *d);
};

struct iio_mutex * iio_mutex_create(void)
{
	struct iio_mutex *lock = malloc(sizeof(*lock));

	if (!lock)
		return iio_ptr(-ENOMEM);

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
		return iio_ptr(-ENOMEM);

	InitializeConditionVariable(&cond->cond);

	return cond;
}

void iio_cond_destroy(struct iio_cond *cond)
{
	free(cond);
}

int iio_cond_wait(struct iio_cond *cond, struct iio_mutex *lock,
		  unsigned int timeout_ms)
{
	BOOL ret;

	if (timeout_ms == 0)
		timeout_ms = INFINITE;

	ret = SleepConditionVariableCS(&cond->cond, &lock->lock, timeout_ms);

	return ret ? 0 : -ETIMEDOUT;
}

void iio_cond_signal(struct iio_cond *cond)
{
	WakeConditionVariable(&cond->cond);
}

static DWORD iio_thrd_wrapper(void *d)
{
	struct iio_thrd *thrd = d;

	return (DWORD) thrd->func(thrd->d);
}

struct iio_thrd * iio_thrd_create(int (*thrd)(void *),
				  void *d, const char *name)
{
	struct iio_thrd *iio_thrd;

	if (!thrd)
		return iio_ptr(-EINVAL);

	iio_thrd = malloc(sizeof(*iio_thrd));
	if (!iio_thrd)
		return iio_ptr(-ENOMEM);

	iio_thrd->func = thrd;
	iio_thrd->d = d;

	iio_thrd->thid = CreateThread(NULL, 0,
				      (LPTHREAD_START_ROUTINE) iio_thrd_wrapper,
				      d, 0, NULL);
	if (!iio_thrd->thid) {
		free(iio_thrd);
		return iio_ptr(-(int) GetLastError());
	}

	/* TODO: set name */
	//SetThreadDescription(thrd->thid, name);

	return iio_thrd;
}

int iio_thrd_join_and_destroy(struct iio_thrd *thrd)
{
	DWORD ret = 0;

	WaitForSingleObject(thrd->thid, INFINITE);
	GetExitCodeThread(thrd->thid, &ret);
	free(thrd);

	return (int) ret;
}
