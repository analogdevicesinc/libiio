// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2015 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "iio-config.h"

#include <iio/iio-backend.h>
#include <iio/iio-lock.h>

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

struct iio_mutex {
	pthread_mutex_t lock;
};

struct iio_cond {
	pthread_cond_t cond;
};

struct iio_thrd {
	pthread_t thid;
	void *d;
	int (*func)(void *);
};

struct iio_mutex * iio_mutex_create(void)
{
	struct iio_mutex *lock = malloc(sizeof(*lock));

	if (!lock)
		return iio_ptr(-ENOMEM);

	pthread_mutex_init(&lock->lock, NULL);
	return lock;
}

void iio_mutex_destroy(struct iio_mutex *lock)
{
	pthread_mutex_destroy(&lock->lock);
	free(lock);
}

void iio_mutex_lock(struct iio_mutex *lock)
{
	pthread_mutex_lock(&lock->lock);
}

void iio_mutex_unlock(struct iio_mutex *lock)
{
	pthread_mutex_unlock(&lock->lock);
}

struct iio_cond * iio_cond_create(void)
{
	struct iio_cond *cond = malloc(sizeof(*cond));

	if (!cond)
		return iio_ptr(-ENOMEM);

	pthread_cond_init(&cond->cond, NULL);

	return cond;
}

void iio_cond_destroy(struct iio_cond *cond)
{
	pthread_cond_destroy(&cond->cond);
	free(cond);
}

int iio_cond_wait(struct iio_cond *cond, struct iio_mutex *lock,
		  unsigned int timeout_ms)
{
	struct timespec ts;
	uint64_t usec;
	int ret = 0;

	if (timeout_ms == 0) {
		pthread_cond_wait(&cond->cond, &lock->lock);
	} else {
		clock_gettime(CLOCK_REALTIME, &ts);

		usec = ts.tv_sec * 1000000ull + ts.tv_nsec / 1000;
		usec += timeout_ms * 1000ull;

		ts.tv_sec = usec / 1000000;
		ts.tv_nsec = (usec % 1000000) * 1000;

		ret = - pthread_cond_timedwait(&cond->cond, &lock->lock, &ts);
	}

	return ret;
}

void iio_cond_signal(struct iio_cond *cond)
{
	pthread_cond_signal(&cond->cond);
}

static void * iio_thrd_wrapper(void *d)
{
	struct iio_thrd *thrd = d;

	return (void *)(intptr_t) thrd->func(thrd->d);
}

struct iio_thrd * iio_thrd_create(int (*thrd)(void *),
				  void *d, const char *name)
{
	struct iio_thrd *iio_thrd;
	int ret;

	if (!thrd)
		return iio_ptr(-EINVAL);

	iio_thrd = malloc(sizeof(*iio_thrd));
	if (!iio_thrd)
		return iio_ptr(-ENOMEM);

	iio_thrd->d = d;
	iio_thrd->func = thrd;

	ret = pthread_create(&iio_thrd->thid, NULL,
			     iio_thrd_wrapper, iio_thrd);
	if (ret) {
		free(iio_thrd);
		return iio_ptr(ret);
	}

	/* TODO: Set name */

	return iio_thrd;
}

int iio_thrd_join_and_destroy(struct iio_thrd *thrd)
{
	void *retval = NULL;
	int ret;

	ret = pthread_join(thrd->thid, &retval);
	if (ret < 0)
		return ret;

	free(thrd);

	return (int)(intptr_t) retval;
}
