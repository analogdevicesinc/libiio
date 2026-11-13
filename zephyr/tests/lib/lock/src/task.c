/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests for the iio_task layer. Under NO_THREADS the task body runs inline on
 * the enqueuing thread and iio_task_start() drains the queue synchronously.
 */

#include <iio/iio.h>
#include <iio/iio-lock.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <errno.h>

#define BLOCK_MS 200

struct task_ctx {
	k_tid_t ran_on;
	struct k_sem started;
	struct k_sem release;
};

static int record_thread_fn(void *first, void *elm)
{
	struct task_ctx *ctx = first;

	ARG_UNUSED(elm);
	ctx->ran_on = k_current_get();

	return 1234;
}

ZTEST(libiio_task, test_task_runs_on_worker_thread)
{
	struct task_ctx ctx = { .ran_on = NULL };
	struct iio_task_token *token;
	struct iio_task *task;

	task = iio_task_create(record_thread_fn, &ctx, "record-task");
	zassert_ok(iio_err(task), "iio_task_create() failed");

	iio_task_start(task);

	token = iio_task_enqueue(task, NULL);
	zassert_ok(iio_err(token), "iio_task_enqueue() failed");

	zassert_equal(iio_task_sync(token, -1), 1234, "task return value not propagated");

	/* Must not have run inline on this thread, as NO_THREADS would. */
	zassert_not_null(ctx.ran_on, "task body never ran");
	zassert_not_equal(ctx.ran_on, k_current_get(),
			  "task body ran inline on the enqueuing thread");

	zassert_ok(iio_task_destroy(task));
}

static int blocking_fn(void *first, void *elm)
{
	struct task_ctx *ctx = first;

	ARG_UNUSED(elm);
	k_sem_give(&ctx->started);
	k_sem_take(&ctx->release, K_FOREVER);

	return 99;
}

static int delayed_release_thread(void *d)
{
	struct task_ctx *ctx = d;

	k_msleep(BLOCK_MS);
	k_sem_give(&ctx->release);

	return 0;
}

ZTEST(libiio_task, test_task_sync_timeout_cancels_pending)
{
	struct task_ctx ctx = { .ran_on = NULL };
	struct iio_task_token *token;
	struct iio_task *task;
	int64_t elapsed;
	int ret;

	task = iio_task_create(record_thread_fn, &ctx, "pending-task");
	zassert_ok(iio_err(task));

	/* Leave the task stopped so the token stays queued and can be cancelled. */
	token = iio_task_enqueue(task, NULL);
	zassert_ok(iio_err(token));

	elapsed = k_uptime_get();
	ret = iio_task_sync(token, 100);
	elapsed = k_uptime_get() - elapsed;

	zassert_equal(ret, -ETIMEDOUT, "expected -ETIMEDOUT, got %d", ret);
	zassert_true(elapsed >= 100, "returned after only %lld ms", elapsed);
	zassert_is_null(ctx.ran_on, "task body ran even though it was cancelled");

	zassert_ok(iio_task_destroy(task));
}

ZTEST(libiio_task, test_task_sync_waits_for_inflight_body)
{
	struct task_ctx ctx;
	struct iio_task_token *token;
	struct iio_task *task;
	struct iio_thrd *releaser;
	int64_t elapsed;
	int ret;

	k_sem_init(&ctx.started, 0, 1);
	k_sem_init(&ctx.release, 0, 1);

	task = iio_task_create(blocking_fn, &ctx, "blocking-task");
	zassert_ok(iio_err(task));
	iio_task_start(task);

	token = iio_task_enqueue(task, NULL);
	zassert_ok(iio_err(token));

	/* Make sure the body is genuinely in flight before syncing. */
	zassert_ok(k_sem_take(&ctx.started, K_MSEC(1000)), "task body never started");

	releaser = iio_thrd_create(delayed_release_thread, &ctx, "releaser");
	zassert_ok(iio_err(releaser));

	/*
	 * task.c drops the cancel once a token leaves the queue, so sync loops
	 * until the body completes. A short timeout is not an escape hatch.
	 */
	elapsed = k_uptime_get();
	ret = iio_task_sync(token, 10);
	elapsed = k_uptime_get() - elapsed;

	zassert_equal(ret, 99, "expected the body's return value, got %d", ret);
	zassert_true(elapsed >= BLOCK_MS, "sync did not wait for the body (%lld ms)", elapsed);

	zassert_ok(iio_thrd_join_and_destroy(releaser));
	zassert_ok(iio_task_destroy(task));
}

static atomic_t autoclear_ran;

static int autoclear_fn(void *first, void *elm)
{
	ARG_UNUSED(first);
	ARG_UNUSED(elm);
	atomic_set(&autoclear_ran, 1);

	return 0;
}

ZTEST(libiio_task, test_task_enqueue_autoclear)
{
	struct iio_task *task;
	unsigned int i;

	atomic_set(&autoclear_ran, 0);

	task = iio_task_create(autoclear_fn, NULL, "autoclear-task");
	zassert_ok(iio_err(task));
	iio_task_start(task);

	zassert_ok(iio_task_enqueue_autoclear(task, NULL));

	for (i = 0; i < 100 && !atomic_get(&autoclear_ran); i++)
		k_msleep(10);

	zassert_true(atomic_get(&autoclear_ran), "autoclear task body never ran");
	zassert_ok(iio_task_destroy(task));
}

ZTEST(libiio_task, test_task_stop_waits_for_inflight_body)
{
	struct task_ctx ctx;
	struct iio_task_token *token;
	struct iio_task *task;
	struct iio_thrd *releaser;
	int64_t elapsed;

	k_sem_init(&ctx.started, 0, 1);
	k_sem_init(&ctx.release, 0, 1);

	task = iio_task_create(blocking_fn, &ctx, "stop-task");
	zassert_ok(iio_err(task));
	iio_task_start(task);

	token = iio_task_enqueue(task, NULL);
	zassert_ok(iio_err(token));
	zassert_ok(k_sem_take(&ctx.started, K_MSEC(1000)), "task body never started");

	/*
	 * Nothing but iio_task_stop() may block inside the measured window, so
	 * the body is released from another thread and the clock starts before
	 * that thread exists. Sleeping here instead would satisfy the assertion
	 * on its own, whatever stop() did.
	 */
	elapsed = k_uptime_get();
	releaser = iio_thrd_create(delayed_release_thread, &ctx, "releaser");
	zassert_ok(iio_err(releaser));

	/* stop() waits for the worker's idle handshake, so it cannot return early. */
	iio_task_stop(task);
	elapsed = k_uptime_get() - elapsed;

	zassert_true(elapsed >= BLOCK_MS, "iio_task_stop() returned too early (%lld ms)", elapsed);

	/* The body ran to completion, so sync just reaps the token's pool slots. */
	zassert_equal(iio_task_sync(token, -1), 99, "task return value not propagated");

	zassert_ok(iio_thrd_join_and_destroy(releaser));
	zassert_ok(iio_task_destroy(task));
}

ZTEST_SUITE(libiio_task, NULL, NULL, NULL, NULL, NULL);
