/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <iio/iio.h>
#include <iio/iio-lock.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/bitarray.h>
#include <zephyr/toolchain.h>

#include <errno.h>

LOG_MODULE_REGISTER(libiio_lock, CONFIG_LIBIIO_LOG_LEVEL);

struct iio_thrd {
	struct k_thread thread;
	size_t slot;
	int (*fn)(void *);
	void *arg;
	int ret;
};

static struct k_mutex iio_mutex_pool[CONFIG_LIBIIO_MUTEX_POOL_SIZE];
SYS_BITARRAY_DEFINE_STATIC(iio_mutex_bitarray, CONFIG_LIBIIO_MUTEX_POOL_SIZE);

static struct k_condvar iio_cond_pool[CONFIG_LIBIIO_COND_POOL_SIZE];
SYS_BITARRAY_DEFINE_STATIC(iio_cond_bitarray, CONFIG_LIBIIO_COND_POOL_SIZE);

static K_KERNEL_STACK_ARRAY_DEFINE(iio_thread_stacks, CONFIG_LIBIIO_THREAD_POOL_SIZE,
				   CONFIG_LIBIIO_THREAD_STACK_SIZE);

static struct iio_thrd iio_thread_pool[CONFIG_LIBIIO_THREAD_POOL_SIZE];
SYS_BITARRAY_DEFINE_STATIC(iio_thread_bitarray, CONFIG_LIBIIO_THREAD_POOL_SIZE);

struct iio_mutex *iio_mutex_create(void)
{
	size_t bit;

	if (sys_bitarray_alloc(&iio_mutex_bitarray, 1, &bit) < 0) {
		LOG_ERR("Out of iio_mutex slots, raise CONFIG_LIBIIO_MUTEX_POOL_SIZE (%d)",
			CONFIG_LIBIIO_MUTEX_POOL_SIZE);
		return iio_ptr(-ENOMEM);
	}

	k_mutex_init(&iio_mutex_pool[bit]);

	return (struct iio_mutex *)&iio_mutex_pool[bit];
}

void iio_mutex_destroy(struct iio_mutex *lock)
{
	struct k_mutex *m = (struct k_mutex *)lock;
	int ret;

	__ASSERT(IS_ARRAY_ELEMENT(iio_mutex_pool, m),
		 "iio_mutex_destroy() on a pointer outside the pool");

	/* -EFAULT means the slot was already free, so two owners now share it. */
	ret = sys_bitarray_free(&iio_mutex_bitarray, 1, m - iio_mutex_pool);
	__ASSERT(ret == 0, "iio_mutex slot %d was not allocated (%d)",
		 (int)(m - iio_mutex_pool), ret);
	ARG_UNUSED(ret);
}

void iio_mutex_lock(struct iio_mutex *lock)
{
	struct k_mutex *m = (struct k_mutex *)lock;

	__ASSERT(IS_ARRAY_ELEMENT(iio_mutex_pool, m),
		 "iio_mutex_lock() on a pointer outside the pool");

	k_mutex_lock(m, K_FOREVER);
}

void iio_mutex_unlock(struct iio_mutex *lock)
{
	struct k_mutex *m = (struct k_mutex *)lock;

	__ASSERT(IS_ARRAY_ELEMENT(iio_mutex_pool, m),
		 "iio_mutex_unlock() on a pointer outside the pool");

	k_mutex_unlock(m);
}

struct iio_cond *iio_cond_create(void)
{
	size_t bit;

	if (sys_bitarray_alloc(&iio_cond_bitarray, 1, &bit) < 0) {
		LOG_ERR("Out of iio_cond slots, raise CONFIG_LIBIIO_COND_POOL_SIZE (%d)",
			CONFIG_LIBIIO_COND_POOL_SIZE);
		return iio_ptr(-ENOMEM);
	}

	k_condvar_init(&iio_cond_pool[bit]);

	return (struct iio_cond *)&iio_cond_pool[bit];
}

void iio_cond_destroy(struct iio_cond *cond)
{
	struct k_condvar *c = (struct k_condvar *)cond;
	int ret;

	__ASSERT(IS_ARRAY_ELEMENT(iio_cond_pool, c),
		 "iio_cond_destroy() on a pointer outside the pool");

	ret = sys_bitarray_free(&iio_cond_bitarray, 1, c - iio_cond_pool);
	__ASSERT(ret == 0, "iio_cond slot %d was not allocated (%d)",
		 (int)(c - iio_cond_pool), ret);
	ARG_UNUSED(ret);
}

int iio_cond_wait(struct iio_cond *cond, struct iio_mutex *lock, int timeout_ms)
{
	struct k_condvar *c = (struct k_condvar *)cond;
	struct k_mutex *m = (struct k_mutex *)lock;
	int ret;

	__ASSERT(IS_ARRAY_ELEMENT(iio_cond_pool, c),
		 "iio_cond_wait() on a pointer outside the pool");

	__ASSERT(IS_ARRAY_ELEMENT(iio_mutex_pool, m),
		 "iio_cond_wait() on a pointer outside the pool");

	/* Do not wait, and keep the mutex held, as the other backends do. */
	if (timeout_ms == 0)
		return -ETIMEDOUT;

	ret = k_condvar_wait(c, m, timeout_ms < 0 ? K_FOREVER : K_MSEC(timeout_ms));

	/* libiio callers expect -ETIMEDOUT, k_condvar_wait() reports -EAGAIN */
	if (ret == -EAGAIN)
		return -ETIMEDOUT;

	return ret;
}

void iio_cond_signal(struct iio_cond *cond)
{
	struct k_condvar *c = (struct k_condvar *)cond;

	__ASSERT(IS_ARRAY_ELEMENT(iio_cond_pool, c),
		 "iio_cond_signal() on a pointer outside the pool");

	k_condvar_signal(c);
}

static void iio_thrd_entry(void *p1, void *p2, void *p3)
{
	struct iio_thrd *thrd = p1;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	thrd->ret = thrd->fn(thrd->arg);
}

struct iio_thrd *iio_thrd_create(int (*thrd)(void *), void *d, const char *name)
{
	struct iio_thrd *iio_thrd;
	size_t bit;

	if (!thrd)
		return iio_ptr(-EINVAL);

	if (sys_bitarray_alloc(&iio_thread_bitarray, 1, &bit) < 0) {
		LOG_ERR("Out of iio_thrd slots, raise CONFIG_LIBIIO_THREAD_POOL_SIZE (%d)",
			CONFIG_LIBIIO_THREAD_POOL_SIZE);
		return iio_ptr(-ENOMEM);
	}

	iio_thrd = &iio_thread_pool[bit];
	iio_thrd->slot = bit;
	iio_thrd->fn = thrd;
	iio_thrd->arg = d;
	iio_thrd->ret = 0;

	k_thread_create(&iio_thrd->thread, iio_thread_stacks[bit],
			K_KERNEL_STACK_SIZEOF(iio_thread_stacks[bit]),
			iio_thrd_entry, iio_thrd, NULL, NULL,
			CONFIG_LIBIIO_THREAD_PRIORITY, 0, K_NO_WAIT);

	if (IS_ENABLED(CONFIG_THREAD_NAME) && name)
		(void)k_thread_name_set(&iio_thrd->thread, name);

	return iio_thrd;
}

int iio_thrd_join_and_destroy(struct iio_thrd *thrd)
{
	int ret, err;

	__ASSERT(IS_ARRAY_ELEMENT(iio_thread_pool, thrd),
		 "iio_thrd_join_and_destroy() on a pointer outside the pool");

	k_thread_join(&thrd->thread, K_FOREVER);
	ret = thrd->ret;

	err = sys_bitarray_free(&iio_thread_bitarray, 1, thrd->slot);
	__ASSERT(err == 0, "iio_thrd slot %d was not allocated (%d)", (int)thrd->slot, err);
	ARG_UNUSED(err);

	return ret;
}
