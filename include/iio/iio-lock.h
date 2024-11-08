/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2015 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#ifndef _IIO_LOCK_H
#define _IIO_LOCK_H

#include <iio/iio.h>

#define __api __iio_api

struct iio_mutex;
struct iio_cond;
struct iio_task;
struct iio_task_token;
struct iio_thrd;

__api struct iio_mutex *iio_mutex_create(void);
__api void iio_mutex_destroy(struct iio_mutex *lock);

__api void iio_mutex_lock(struct iio_mutex *lock);
__api void iio_mutex_unlock(struct iio_mutex *lock);

__api struct iio_cond * iio_cond_create(void);
__api void iio_cond_destroy(struct iio_cond *cond);

__api int iio_cond_wait(struct iio_cond *cond, struct iio_mutex *lock,
			unsigned int timeout_ms);
__api void iio_cond_signal(struct iio_cond *cond);

__api struct iio_thrd * iio_thrd_create(int (*thrd)(void *),
					void *d, const char *name);
__api int iio_thrd_join_and_destroy(struct iio_thrd *thrd);

__api struct iio_task * iio_task_create(int (*task)(void *firstarg, void *d),
					void *firstarg, const char *name);
__api void iio_task_flush(struct iio_task *task);
__api int iio_task_destroy(struct iio_task *task);

__api void iio_task_start(struct iio_task *task);
__api void iio_task_stop(struct iio_task *task);

__api struct iio_task_token * iio_task_enqueue(struct iio_task *task, void *elm);
__api int iio_task_enqueue_autoclear(struct iio_task *task, void *elm);

__api _Bool iio_task_is_done(struct iio_task_token *token);
__api int iio_task_sync(struct iio_task_token *token, unsigned int timeout_ms);
__api void iio_task_cancel(struct iio_task_token *token);

#undef __api

#endif /* _IIO_LOCK_H */
