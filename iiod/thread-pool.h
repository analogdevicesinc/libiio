/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2016 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#ifndef __THREAD_POOL_H__
#define __THREAD_POOL_H__

#include <stdbool.h>

struct thread_pool;

struct thread_pool * thread_pool_new(void);

int thread_pool_get_poll_fd(const struct thread_pool *pool);
void thread_pool_stop(struct thread_pool *pool);
void thread_pool_stop_and_wait(struct thread_pool *pool);
bool thread_pool_is_stopped(const struct thread_pool *pool);

void thread_pool_destroy(struct thread_pool *pool);

int thread_pool_add_thread(struct thread_pool *pool,
		void (*func)(struct thread_pool *, void *),
		void *data, const char *name);

#endif /* __THREAD_POOL_H__ */
