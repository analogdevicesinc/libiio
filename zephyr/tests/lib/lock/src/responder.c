/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests for how the iiod responder reports a pool-exhaustion failure. Zephyr is
 * the only platform where this is reachable: iio_cond_create() draws from a
 * fixed pool here, where the pthread backend allocates.
 */

#include <iio/iio.h>
#include <iio/iio-lock.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <errno.h>

#include "iiod-responder.h"

/* Parks the responder's reader thread so the responder stays live for the test:
 * a read that returns <= 0 sets thrd_stop, and iiod_enqueue_command() would then
 * refuse before it ever reaches the enqueue this test is about.
 */
static K_SEM_DEFINE(reader_parked, 0, 1);

static ssize_t loopback_read(void *d, const struct iiod_buf *buf, size_t nb)
{
	ARG_UNUSED(d);
	ARG_UNUSED(buf);
	ARG_UNUSED(nb);

	k_sem_take(&reader_parked, K_FOREVER);

	return 0;
}

static ssize_t loopback_write(void *d, const struct iiod_buf *buf, size_t nb)
{
	size_t i, total = 0;

	ARG_UNUSED(d);

	for (i = 0; i < nb; i++)
		total += buf[i].size;

	return (ssize_t)total;
}

static ssize_t loopback_discard(void *d, size_t bytes)
{
	ARG_UNUSED(d);

	return (ssize_t)bytes;
}

static int loopback_cmd(const struct iiod_command *cmd, struct iiod_command_data *data, void *d)
{
	ARG_UNUSED(cmd);
	ARG_UNUSED(data);
	ARG_UNUSED(d);

	return -EINVAL;
}

static const struct iiod_responder_ops loopback_ops = {
	.cmd = loopback_cmd,
	.read = loopback_read,
	.write = loopback_write,
	.discard = loopback_discard,
};

/* Take every remaining iio_cond slot, so the next iio_cond_create() fails. */
static unsigned int drain_cond_pool(struct iio_cond **held, unsigned int max)
{
	unsigned int n = 0;

	while (n < max) {
		struct iio_cond *cond = iio_cond_create();

		if (iio_err(cond))
			break;

		held[n++] = cond;
	}

	return n;
}

static void release_cond_pool(struct iio_cond **held, unsigned int n)
{
	while (n--)
		iio_cond_destroy(held[n]);
}

/*
 * A response whose write token cannot be created must be refused, not
 * remembered. iio_task_enqueue() reports the failure as an error pointer, and
 * every later reader of iiod_io::write_token tests it for NULL before
 * dereferencing it - so storing the error pointer turns a refused command into
 * a fault on the next round trip, and leaves the io permanently -EIO because the
 * "already enqueued" guard sees a non-NULL token.
 */
ZTEST(libiio_responder, test_enqueue_failure_is_reported_not_stored)
{
	struct iio_cond *held[CONFIG_LIBIIO_COND_POOL_SIZE];
	struct iiod_responder *responder;
	struct iiod_io *io;
	unsigned int n;
	int ret;

	k_sem_reset(&reader_parked);

	responder = iiod_responder_create(&loopback_ops, NULL);
	zassert_ok(iio_err(responder), "iiod_responder_create() failed (%d)",
		   iio_err(responder));

	io = iiod_responder_get_default_io(responder);
	zassert_not_null(io, "responder has no default io");

	n = drain_cond_pool(held, ARRAY_SIZE(held));
	zassert_true(n > 0, "cond pool already exhausted; the send would fail for the wrong reason");

	ret = iiod_io_send_response_async(io, 0, NULL, 0);
	zassert_equal(ret, -ENOMEM, "expected -ENOMEM with the cond pool drained, got %d", ret);

	/* Nothing was enqueued, so there is nothing to wait for. This faults if
	 * the error pointer was stored: iio_task_sync() dereferences it.
	 */
	zassert_ok(iiod_io_wait_for_command_done(io),
		   "wait_for_command_done() reported work that was never enqueued");

	/* Same dereference through the cancel path. */
	iiod_io_cancel(io);

	release_cond_pool(held, n);

	/* The refused send must not have wedged the io. A stored token makes this
	 * -EIO forever, which no client can distinguish from a transport error.
	 */
	ret = iiod_io_send_response_async(io, 0, NULL, 0);
	zassert_ok(ret, "send after a refused send failed (%d); the io kept a token", ret);

	zassert_ok(iiod_io_wait_for_command_done(io));

	k_sem_give(&reader_parked);
	iiod_responder_stop(responder);
	iiod_responder_wait_done(responder);
	iiod_responder_destroy(responder);
}

ZTEST_SUITE(libiio_responder, NULL, NULL, NULL, NULL, NULL);
