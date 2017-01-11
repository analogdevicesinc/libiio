/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2016 Analog Devices, Inc.
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
 */

#include "thread-pool.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/eventfd.h>
#include <unistd.h>

/*
 * This is used to make sure that all active threads have finished cleanup when
 * a STOP event is received. We don't use pthread_join() since for most threads
 * we are OK with them exiting asynchronously and there really is no place to
 * call pthread_join() to free the thread's resources. We only need to
 * synchronize the threads that are still active when the iiod is shutdown to
 * give them a chance to release all resources, disable buffers etc, before
 * iio_context_destroy() is called.
 */

struct thread_pool {
	pthread_mutex_t thread_count_lock;
	pthread_cond_t thread_count_cond;
	unsigned int thread_count;
	int stop_fd;
};

struct thread_body_data {
	struct thread_pool *pool;
	void (*f)(struct thread_pool *, void *);
	void *d;
};

static void thread_pool_thread_started(struct thread_pool *pool)
{
	pthread_mutex_lock(&pool->thread_count_lock);
	pool->thread_count++;
	pthread_mutex_unlock(&pool->thread_count_lock);
}

static void thread_pool_thread_stopped(struct thread_pool *pool)
{
	pthread_mutex_lock(&pool->thread_count_lock);
	pool->thread_count--;
	pthread_cond_signal(&pool->thread_count_cond);
	pthread_mutex_unlock(&pool->thread_count_lock);
}

static void * thread_body(void *d)
{
	struct thread_body_data *pdata = d;

	(*pdata->f)(pdata->pool, pdata->d);

	thread_pool_thread_stopped(pdata->pool);
	free(pdata);

	return NULL;
}

int thread_pool_add_thread(struct thread_pool *pool,
		void (*f)(struct thread_pool *, void *),
		void *d, const char *name)
{
	struct thread_body_data *pdata;
	sigset_t sigmask, oldsigmask;
	pthread_attr_t attr;
	pthread_t thd;
	int ret;

	pdata = malloc(sizeof(*pdata));
	if (!pdata)
		return -ENOMEM;

	pdata->f = f;
	pdata->d = d;
	pdata->pool = pool;

	sigfillset(&sigmask);
	pthread_sigmask(SIG_BLOCK, &sigmask, &oldsigmask);

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	/* In order to avoid race conditions thread_pool_thread_started() must
	 * be called before the thread is created and
	 * thread_pool_thread_stopped() must be called right before leaving
	 * the thread. */
	thread_pool_thread_started(pool);

	ret = pthread_create(&thd, &attr, thread_body, pdata);
	if (ret) {
		free(pdata);
		thread_pool_thread_stopped(pool);
	} else {
#ifdef HAS_PTHREAD_SETNAME_NP
		pthread_setname_np(thd, name);
#endif
	}

	pthread_attr_destroy(&attr);
	pthread_sigmask(SIG_SETMASK, &oldsigmask, NULL);
	return ret;
}

struct thread_pool * thread_pool_new(void)
{
	struct thread_pool *pool;

	pool = malloc(sizeof(*pool));
	if (!pool) {
		errno = ENOMEM;
		return NULL;
	}

	pool->stop_fd = eventfd(0, EFD_NONBLOCK);
	if (pool->stop_fd == -1) {
		int err = errno;

		free(pool);
		errno = err;
		return NULL;
	}

	pthread_mutex_init(&pool->thread_count_lock, NULL);
	pthread_cond_init(&pool->thread_count_cond, NULL);
	pool->thread_count = 0;

	return pool;
}

int thread_pool_get_poll_fd(const struct thread_pool *pool)
{
	return pool->stop_fd;
}

void thread_pool_stop(struct thread_pool *pool)
{
	uint64_t e = 1;
	int ret;

	do {
		ret = write(pool->stop_fd, &e, sizeof(e));
	} while (ret == -1 && errno == EINTR);
}

void thread_pool_stop_and_wait(struct thread_pool *pool)
{
	uint64_t e;
	int ret;

	thread_pool_stop(pool);

	pthread_mutex_lock(&pool->thread_count_lock);
	while (pool->thread_count)
		pthread_cond_wait(&pool->thread_count_cond,
				&pool->thread_count_lock);
	pthread_mutex_unlock(&pool->thread_count_lock);

	do {
		ret = read(pool->stop_fd, &e, sizeof(e));
	} while (ret != -1 || errno == EINTR);
}

void thread_pool_destroy(struct thread_pool *pool)
{
	pthread_mutex_destroy(&pool->thread_count_lock);
	pthread_cond_destroy(&pool->thread_count_cond);

	close(pool->stop_fd);
	free(pool);
}
