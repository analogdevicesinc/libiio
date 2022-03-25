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

#ifndef NO_THREADS
#include <pthread.h>
#endif

#include <errno.h>
#include <stdlib.h>

struct iio_mutex {
#ifdef NO_THREADS
	int foo; /* avoid complaints about empty structure */
#else
	pthread_mutex_t lock;
#endif
};

struct iio_cond {
#ifdef NO_THREADS
	int foo; /* avoid complaints about empty structure */
#else
	pthread_cond_t cond;
#endif
};

struct iio_thrd {
#ifndef NO_THREADS
	pthread_t thid;
#endif
	void *d;
	int (*func)(void *);
};

struct iio_mutex * iio_mutex_create(void)
{
	struct iio_mutex *lock = malloc(sizeof(*lock));

	if (!lock)
		return iio_ptr(-ENOMEM);

#ifndef NO_THREADS
	pthread_mutex_init(&lock->lock, NULL);
#endif
	return lock;
}

void iio_mutex_destroy(struct iio_mutex *lock)
{
#ifndef NO_THREADS
	pthread_mutex_destroy(&lock->lock);
#endif
	free(lock);
}

void iio_mutex_lock(struct iio_mutex *lock)
{
#ifndef NO_THREADS
	pthread_mutex_lock(&lock->lock);
#endif
}

void iio_mutex_unlock(struct iio_mutex *lock)
{
#ifndef NO_THREADS
	pthread_mutex_unlock(&lock->lock);
#endif
}

struct iio_cond * iio_cond_create(void)
{
	struct iio_cond *cond = malloc(sizeof(*cond));

	if (!cond)
		return iio_ptr(-ENOMEM);

#ifndef NO_THREADS
	pthread_cond_init(&cond->cond, NULL);
#endif

	return cond;
}

void iio_cond_destroy(struct iio_cond *cond)
{
#ifndef NO_THREADS
	pthread_cond_destroy(&cond->cond);
#endif
	free(cond);
}

void iio_cond_wait(struct iio_cond *cond, struct iio_mutex *lock)
{
#ifndef NO_THREADS
	pthread_cond_wait(&cond->cond, &lock->lock);
#endif
}

void iio_cond_signal(struct iio_cond *cond)
{
#ifndef NO_THREADS
	pthread_cond_signal(&cond->cond);
#endif
}

#ifndef NO_THREADS
static void * iio_thrd_wrapper(void *d)
{
	struct iio_thrd *thrd = d;

	return (void *)(intptr_t) thrd->func(thrd->d);
}
#endif

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

#ifndef NO_THREADS
	ret = pthread_create(&iio_thrd->thid, NULL,
			     iio_thrd_wrapper, iio_thrd);
	if (ret) {
		free(iio_thrd);
		return iio_ptr(ret);
	}

	/* TODO: Set name */
#endif

	return iio_thrd;
}

int iio_thrd_join_and_destroy(struct iio_thrd *thrd)
{
	void *retval = NULL;
	int ret;

#ifndef NO_THREADS
	ret = pthread_join(thrd->thid, &retval);
	if (ret < 0)
		return ret;
#endif

	free(thrd);

	return (int)(intptr_t) retval;
}
