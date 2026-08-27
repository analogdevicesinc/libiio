/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests for the Zephyr implementation of the libiio lock and thread API.
 */

#include <iio/iio.h>
#include <iio/iio-lock.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <errno.h>

#ifndef CONFIG_LIBIIO_MULTITHREADING
#error "This suite tests the threaded lock backend; enable CONFIG_LIBIIO_MULTITHREADING"
#endif

#define ITERATIONS 2000

struct counter_ctx {
	struct iio_mutex *lock;
	volatile unsigned int count;
};

static int counter_thread(void *d)
{
	struct counter_ctx *ctx = d;
	unsigned int i;

	for (i = 0; i < ITERATIONS; i++) {
		iio_mutex_lock(ctx->lock);
		ctx->count = ctx->count + 1;
		iio_mutex_unlock(ctx->lock);
	}

	return 0;
}

ZTEST(libiio_lock, test_mutex_excludes)
{
	struct counter_ctx ctx = { .count = 0 };
	struct iio_thrd *a, *b;

	ctx.lock = iio_mutex_create();
	zassert_ok(iio_err(ctx.lock), "iio_mutex_create() failed");

	a = iio_thrd_create(counter_thread, &ctx, "counter-a");
	zassert_ok(iio_err(a), "iio_thrd_create() failed");
	b = iio_thrd_create(counter_thread, &ctx, "counter-b");
	zassert_ok(iio_err(b), "iio_thrd_create() failed");

	zassert_ok(iio_thrd_join_and_destroy(a));
	zassert_ok(iio_thrd_join_and_destroy(b));

	/* The no-op backend loses updates here. */
	zassert_equal(ctx.count, 2 * ITERATIONS, "lost updates: %u != %u", ctx.count,
		      2 * ITERATIONS);

	iio_mutex_destroy(ctx.lock);
}

ZTEST(libiio_lock, test_mutex_pool_exhaustion_and_reuse)
{
	struct iio_mutex *held[CONFIG_LIBIIO_MUTEX_POOL_SIZE];
	struct iio_mutex *extra;
	unsigned int n = 0, i;

	/* Count rather than assume the pool starts empty. */
	while (n < ARRAY_SIZE(held)) {
		struct iio_mutex *m = iio_mutex_create();

		if (iio_err(m))
			break;
		held[n++] = m;
	}

	zassert_true(n > 0, "could not allocate a single mutex");

	extra = iio_mutex_create();
	zassert_equal(iio_err(extra), -ENOMEM, "expected -ENOMEM once exhausted, got %d",
		      iio_err(extra));

	for (i = 0; i < n; i++)
		iio_mutex_destroy(held[i]);

	/* Slots must be reusable, not leaked. */
	extra = iio_mutex_create();
	zassert_ok(iio_err(extra), "slot was not released back to the pool");
	iio_mutex_destroy(extra);
}

struct cond_ctx {
	struct iio_mutex *lock;
	struct iio_cond *cond;
};

static int signal_thread(void *d)
{
	struct cond_ctx *ctx = d;

	/* The waiter holds the mutex until it waits, so this cannot be lost. */
	iio_mutex_lock(ctx->lock);
	iio_cond_signal(ctx->cond);
	iio_mutex_unlock(ctx->lock);

	return 0;
}

ZTEST(libiio_lock, test_cond_timeout_zero_does_not_block)
{
	struct cond_ctx ctx;
	int64_t start;
	int ret;

	ctx.lock = iio_mutex_create();
	zassert_ok(iio_err(ctx.lock));
	ctx.cond = iio_cond_create();
	zassert_ok(iio_err(ctx.cond));

	iio_mutex_lock(ctx.lock);
	start = k_uptime_get();
	ret = iio_cond_wait(ctx.cond, ctx.lock, 0);
	zassert_equal(ret, -ETIMEDOUT, "expected -ETIMEDOUT, got %d", ret);
	zassert_true(k_uptime_get() - start < 10, "a zero timeout must not block");
	iio_mutex_unlock(ctx.lock);

	iio_cond_destroy(ctx.cond);
	iio_mutex_destroy(ctx.lock);
}

ZTEST(libiio_lock, test_cond_timeout_expires)
{
	struct cond_ctx ctx;
	int64_t elapsed;
	int ret;

	ctx.lock = iio_mutex_create();
	zassert_ok(iio_err(ctx.lock));
	ctx.cond = iio_cond_create();
	zassert_ok(iio_err(ctx.cond));

	iio_mutex_lock(ctx.lock);
	elapsed = k_uptime_get();
	ret = iio_cond_wait(ctx.cond, ctx.lock, 100);
	elapsed = k_uptime_get() - elapsed;
	iio_mutex_unlock(ctx.lock);

	zassert_equal(ret, -ETIMEDOUT, "expected -ETIMEDOUT, got %d", ret);
	/* The no-op backend returns -ETIMEDOUT without waiting. */
	zassert_true(elapsed >= 100, "returned after only %lld ms", elapsed);

	iio_cond_destroy(ctx.cond);
	iio_mutex_destroy(ctx.lock);
}

ZTEST(libiio_lock, test_cond_signal_wakes_waiter)
{
	struct cond_ctx ctx;
	struct iio_thrd *thrd;
	int ret;

	ctx.lock = iio_mutex_create();
	zassert_ok(iio_err(ctx.lock));
	ctx.cond = iio_cond_create();
	zassert_ok(iio_err(ctx.cond));

	iio_mutex_lock(ctx.lock);

	thrd = iio_thrd_create(signal_thread, &ctx, "signaller");
	zassert_ok(iio_err(thrd));

	ret = iio_cond_wait(ctx.cond, ctx.lock, 5000);
	iio_mutex_unlock(ctx.lock);

	zassert_ok(ret, "wait was not woken by the signal, got %d", ret);
	zassert_ok(iio_thrd_join_and_destroy(thrd));

	iio_cond_destroy(ctx.cond);
	iio_mutex_destroy(ctx.lock);
}

ZTEST(libiio_lock, test_cond_pool_exhaustion_and_reuse)
{
	struct iio_cond *held[CONFIG_LIBIIO_COND_POOL_SIZE];
	struct iio_cond *extra;
	unsigned int n = 0, i;

	while (n < ARRAY_SIZE(held)) {
		struct iio_cond *c = iio_cond_create();

		if (iio_err(c))
			break;
		held[n++] = c;
	}

	zassert_true(n > 0, "could not allocate a single condvar");

	extra = iio_cond_create();
	zassert_equal(iio_err(extra), -ENOMEM, "expected -ENOMEM once exhausted, got %d",
		      iio_err(extra));

	for (i = 0; i < n; i++)
		iio_cond_destroy(held[i]);

	extra = iio_cond_create();
	zassert_ok(iio_err(extra), "slot was not released back to the pool");
	iio_cond_destroy(extra);
}

static int return_value_thread(void *d)
{
	return (int)(intptr_t)d;
}

ZTEST(libiio_lock, test_thrd_returns_value)
{
	struct iio_thrd *thrd;

	thrd = iio_thrd_create(return_value_thread, (void *)(intptr_t)42, "answer");
	zassert_ok(iio_err(thrd));
	zassert_equal(iio_thrd_join_and_destroy(thrd), 42);
}

static K_SEM_DEFINE(park_sem, 0, K_SEM_MAX_LIMIT);

static int parked_thread(void *d)
{
	ARG_UNUSED(d);

	k_sem_take(&park_sem, K_FOREVER);

	return 0;
}

ZTEST(libiio_lock, test_thrd_pool_exhaustion_and_reuse)
{
	struct iio_thrd *held[CONFIG_LIBIIO_THREAD_POOL_SIZE];
	struct iio_thrd *extra;
	unsigned int n = 0, i;

	k_sem_reset(&park_sem);

	while (n < ARRAY_SIZE(held)) {
		struct iio_thrd *t = iio_thrd_create(parked_thread, NULL, "parked");

		if (iio_err(t))
			break;
		held[n++] = t;
	}

	zassert_true(n > 0, "could not create a single thread");

	extra = iio_thrd_create(parked_thread, NULL, "overflow");
	zassert_equal(iio_err(extra), -ENOMEM, "expected -ENOMEM once exhausted, got %d",
		      iio_err(extra));

	/* Release the parked threads, then reclaim the slots. */
	for (i = 0; i < n; i++)
		k_sem_give(&park_sem);

	for (i = 0; i < n; i++)
		zassert_ok(iio_thrd_join_and_destroy(held[i]));

	extra = iio_thrd_create(return_value_thread, (void *)(intptr_t)7, "reused");
	zassert_ok(iio_err(extra), "slot was not released back to the pool");
	zassert_equal(iio_thrd_join_and_destroy(extra), 7);
}

ZTEST_SUITE(libiio_lock, NULL, NULL, NULL, NULL, NULL);
