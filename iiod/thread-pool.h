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

#ifndef __THREAD_POOL_H__
#define __THREAD_POOL_H__

struct thread_pool;

struct thread_pool * thread_pool_new(void);

int thread_pool_get_poll_fd(const struct thread_pool *pool);
void thread_pool_stop(struct thread_pool *pool);
void thread_pool_stop_and_wait(struct thread_pool *pool);

void thread_pool_destroy(struct thread_pool *pool);

int thread_pool_add_thread(struct thread_pool *pool,
		void (*func)(struct thread_pool *, void *),
		void *data, const char *name);

#endif /* __THREAD_POOL_H__ */
