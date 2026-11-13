// SPDX-License-Identifier: MIT
/*
 * iiod-emu - IIO hardware emulator
 *
 * A UDP stream provider that sends nothing and writes one line per call
 * instead, for the test of the responder's rules for providers
 * (tests/daqiri/test_udp_provider_contract.c). Hidden option
 * --udp-mock-provider <log>. The context advertises iiod,udp-stream as with
 * --udp, so that a client which only sends the setup to a server that does,
 * like the daqiri: backend, can also be tested against one that never streams.
 *
 * A setup's line ends in " pending" when the responder still holds an earlier
 * setup of the buffer that no enable has used and no disable has dropped: what
 * a client's DISABLE did shows there, since the responder calls no provider
 * for a DISABLE with nothing streaming.
 *
 * With --udp-mock-fail-disable <n> as well, every DISABLE_BUFFER shows: once
 * the responder has stopped the stream, the emulated device's buffer logs
 * "disable", or for the first <n> of them "disable failed" and answers
 * -ETIMEDOUT, enabled still, as a board does whose sender did not drain.
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 */

#include <errno.h>
#include <iio/iio-lock.h>
#include <iio/iio.h>
#include <iiod/ops.h>
#include <iiod/udp-stream.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "../iiod-responder.h"
#include "udp-stream.h"

/* max_payload values that make the mock misbehave on purpose */
#define SETUP_RETURNS_POSITIVE 4321 /* setup() returns 5 */
#define SETUP_SHORT_TAIL 4322	    /* the answer's tail is cut, see mock_short_tail() */
#define START_FAILS 1234	    /* start() fails with -EADDRINUSE */

/* A layout the mock refuses (-ENOTSUP), as a server may refuse a channel
 * mask it cannot stream: sample sets of 2 bytes, one 16-bit channel */
#define REFUSED_SAMPLE_SIZE 2

/* A layout whose second setup the mock refuses with -EBUSY, the first and the
 * later ones answered: to a client that probes at open, the setup of its first
 * enable fails. Sample sets of 8 bytes, four 16-bit channels. */
#define REFUSED_SECOND_SAMPLE_SIZE 8

/* How long the cut answer stalls before more bytes follow */
#define SHORT_TAIL_STALL_MS 2000

struct emu_udp_mock {
	FILE *log;
	struct iio_mutex *lock;
	uint64_t fail_disable; /* DISABLE_BUFFERs still to fail */
};

/* In entry->udp_priv */
struct emu_udp_mock_buf {
	uint16_t max_payload;
	unsigned int setups; /* setup() calls past the layout check */
};

static void __attribute__((format(printf, 2, 3))) mock_log(
		struct emu_udp_mock *mock, const char *fmt, ...)
{
	va_list ap;

	iio_mutex_lock(mock->lock);

	va_start(ap, fmt);
	vfprintf(mock->log, fmt, ap);
	va_end(ap);

	fputc('\n', mock->log);
	fflush(mock->log);

	iio_mutex_unlock(mock->lock);
}

/*
 * Break the protocol on purpose: answer with code 12, the length of a response
 * tail, followed by only 5 bytes of that tail, so that the client reads them
 * into its buffer for the tail and waits for the rest. SHORT_TAIL_STALL_MS
 * later, setup() fails and the responder's own answer, 8 bytes, completes the
 * tail: a client that gave up in between must no longer have that buffer
 * where the bytes land. The client sends the setup on its default I/O, ID 0.
 */
static int mock_short_tail(struct buffer_entry *entry)
{
	const struct iiod_command answer = {
		.client_id = 0,
		.op = IIOD_OP_RESPONSE,
		.code = IIOD_UDP_STREAM_RESP_LEN,
	};
	static const uint8_t tail[5] = { 0xde, 0xad, 0xbe, 0xef, 0x00 };
	const struct timespec stall = {
		.tv_sec = SHORT_TAIL_STALL_MS / 1000,
		.tv_nsec = (SHORT_TAIL_STALL_MS % 1000) * 1000000L,
	};

	if (write_all(entry->pdata, &answer, sizeof(answer)) < 0 ||
			write_all(entry->pdata, tail, sizeof(tail)) < 0)
		return -EIO;

	nanosleep(&stall, NULL);

	return -EIO;
}

static int mock_setup(void *priv, struct buffer_entry *entry, const struct iiod_udp_stream_req *req,
		struct iiod_udp_stream_resp *resp)
{
	struct emu_udp_mock_buf *mb = entry->udp_priv;
	const struct iio_channels_mask *mask;
	ssize_t sample_size = 0;

	mock_log(priv, "setup %u%s", req->max_payload, entry->udp_setup ? " pending" : "");

	/* Refused before it leaves any state behind */
	if (entry->buf_stream) {
		mask = iio_buffer_stream_get_channels_mask(entry->buf_stream);
		sample_size = iio_device_get_sample_size(entry->dev, mask);
		if (sample_size == REFUSED_SAMPLE_SIZE)
			return -ENOTSUP;
	}

	if (!mb) {
		mb = calloc(1, sizeof(*mb));
		if (!mb)
			return -ENOMEM;
		entry->udp_priv = mb;
	}

	if (++mb->setups == 2 && sample_size == REFUSED_SECOND_SAMPLE_SIZE)
		return -EBUSY;

	mb->max_payload = req->max_payload;

	if (req->max_payload == SETUP_RETURNS_POSITIVE)
		return 5;
	if (req->max_payload == SETUP_SHORT_TAIL)
		return mock_short_tail(entry);

	resp->stream_id = 0x12345678;
	resp->payload_bytes = req->max_payload & ~15u;
	resp->src_ip = 0x7f000001; /* 127.0.0.1 */
	resp->src_port = 25102;

	return 0;
}

static int mock_start(void *priv, struct buffer_entry *entry)
{
	struct emu_udp_mock_buf *mb = entry->udp_priv;

	/* The responder broke its rules: say so rather than crash */
	if (!mb) {
		mock_log(priv, "start without a setup");
		return -EINVAL;
	}

	if (mb->max_payload == START_FAILS) {
		mock_log(priv, "start failed");
		return -EADDRINUSE;
	}

	mock_log(priv, "start");

	return 0;
}

static void mock_stop(void *priv, struct buffer_entry *entry)
{
	mock_log(priv, "stop");
}

static void mock_release(void *priv, struct buffer_entry *entry)
{
	mock_log(priv, "release");

	free(entry->udp_priv);
	entry->udp_priv = NULL;
}

const struct iiod_udp_stream_ops emu_udp_mock_ops = {
	.setup = mock_setup,
	.start = mock_start,
	.stop = mock_stop,
	.release = mock_release,
};

/* --udp-mock-fail-disable: the emulated device's ops, with the enable_buffer()
 * below, which the responder's DISABLE_BUFFER reaches after stop() */
static struct emu_udp_mock *disable_mock;
static const struct iio_backend_ops *device_ops;
static struct iio_backend_ops mock_device_ops;

static int mock_enable_buffer(
		struct iio_buffer_pdata *pdata, size_t nb_samples, bool enable, bool cyclic)
{
	struct emu_udp_mock *mock = disable_mock;
	bool fail;

	if (!enable) {
		iio_mutex_lock(mock->lock);
		fail = mock->fail_disable > 0;
		if (fail)
			mock->fail_disable--;
		iio_mutex_unlock(mock->lock);

		mock_log(mock, fail ? "disable failed" : "disable");
		if (fail)
			return -ETIMEDOUT;
	}

	return device_ops->enable_buffer(pdata, nb_samples, enable, cyclic);
}

const struct iio_backend_ops *emu_udp_mock_device_ops(
		struct emu_udp_mock *mock, const struct iio_backend_ops *ops, uint64_t n)
{
	mock->fail_disable = n;
	disable_mock = mock;

	device_ops = ops;
	mock_device_ops = *ops;
	mock_device_ops.enable_buffer = mock_enable_buffer;

	return &mock_device_ops;
}

struct emu_udp_mock *emu_udp_mock_create(const char *path)
{
	struct emu_udp_mock *mock = calloc(1, sizeof(*mock));

	if (!mock)
		return NULL;

	mock->lock = iio_mutex_create();
	if (iio_err(mock->lock))
		goto err_free_mock;

	/* Empty from the start: the test compares the whole file */
	mock->log = fopen(path, "w");
	if (!mock->log) {
		perror(path);
		goto err_destroy_lock;
	}

	return mock;

err_destroy_lock:
	iio_mutex_destroy(mock->lock);
err_free_mock:
	free(mock);
	return NULL;
}

void emu_udp_mock_destroy(struct emu_udp_mock *mock)
{
	if (!mock)
		return;

	fclose(mock->log);
	iio_mutex_destroy(mock->lock);
	free(mock);
}
