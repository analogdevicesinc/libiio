/* SPDX-License-Identifier: MIT */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 *
 * The responder's rules for a UDP stream provider (iiod/udp-stream.h), seen
 * through iiod-emu --udp-mock-provider <log>: a provider that sends nothing
 * and writes one line per call. After each step the log must have grown by
 * exactly the calls the rules allow (a setup's line says " pending" when an
 * earlier one still is):
 * - start() uses up the pending setup: a second ENABLE starts nothing;
 * - SETUP while a stream is active is answered -EBUSY, with no call;
 * - stop() runs only for an active stream, so not after a failed start();
 * - CLOSE_BUFFER, and the loss of the connection: stop() if the stream is
 *   active, then release().
 *
 * Usage: test_udp_provider_contract <log>
 */

#include <errno.h>
#include <iio/iio.h>
#include <iio/iiod-client.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../iiod-udp-stream.h"
#include "emu_client.h"
#include "test_framework.h"

#define EMU_DEVICE "cf-ad9361-lpc"
#define BLOCK_SIZE 1024
#define MAX_PAYLOAD 8960

/* What the mock provider does for these max_payload values (udp-mock.c) */
#define SETUP_RETURNS_POSITIVE 4321 /* setup() returns 5 */
#define START_FAILS 1234	    /* start() fails with -EADDRINUSE */

/* The provider's teardown runs once the server sees the connection go */
#define TEARDOWN_TIMEOUT_MS 5000

static const char *log_path;
static char expected[4096];

static size_t read_log(char *buf, size_t len)
{
	FILE *f = fopen(log_path, "r");
	size_t n = 0;

	if (f) {
		n = fread(buf, 1, len - 1, f);
		fclose(f);
	}
	buf[n] = '\0';

	return n;
}

/* <calls>, one per line, are what the last step must have added to the log */
static bool log_matches(const char *calls, int timeout_ms, char *log, size_t len)
{
	struct timespec t0, now;
	long ms;

	if (strlen(expected) + strlen(calls) < sizeof(expected))
		strcat(expected, calls);

	clock_gettime(CLOCK_MONOTONIC, &t0);
	for (;;) {
		read_log(log, len);
		if (!strcmp(log, expected))
			return true;

		clock_gettime(CLOCK_MONOTONIC, &now);
		ms = (long)(now.tv_sec - t0.tv_sec) * 1000 + (now.tv_nsec - t0.tv_nsec) / 1000000;
		if (ms >= timeout_ms)
			return false;

		nanosleep(&(struct timespec){ .tv_nsec = 10000000 }, NULL);
	}
}

static void expect_calls_within(const char *calls, int timeout_ms, const char *what)
{
	static char log[sizeof(expected)];
	bool ok = log_matches(calls, timeout_ms, log, sizeof(log));

	TEST_ASSERT(ok, what);
	if (!ok)
		printf("    provider calls so far:\n%s    expected:\n%s", log, expected);
}

static void expect_calls(const char *calls, const char *what)
{
	expect_calls_within(calls, 0, what);
}

static int setup(struct iiod_client_buffer_pdata *buf, uint16_t max_payload,
		struct iiod_udp_stream_resp *resp)
{
	const struct iiod_udp_stream_req req = {
		.dst_ip = 0x7f000001, /* 127.0.0.1 */
		.dst_port = 25101,
		.max_payload = max_payload,
	};

	return iiod_client_udp_stream_setup(buf, &req, resp);
}

static int enable(struct iiod_client_buffer_pdata *buf, bool on)
{
	return iiod_client_enable_buffer(buf, 0, on, false);
}

/* TEST_ASSERT_EQ() evaluates its first argument twice with TESTS_DEBUG, so
 * every call below goes through <ret>. */
static void contract(
		struct emu_client *ec, const struct iio_device *dev, struct iio_channels_mask *mask)
{
	struct iiod_client_buffer_pdata *buf;
	struct iiod_udp_stream_resp resp;
	struct iio_block_pdata *block;
	void *data;
	int ret;

	buf = iiod_client_open_buffer(ec->client, ec->client, dev, 0, mask);
	ret = iio_err(buf);
	TEST_ASSERT_EQ(ret, 0, "buffer opened");
	if (ret)
		return;
	expect_calls("", "open: no call");

	/* The answer: code 12 and the tail, unpacked */
	ret = setup(buf, MAX_PAYLOAD, &resp);
	TEST_ASSERT_EQ(ret, 0, "setup");
	TEST_ASSERT(resp.stream_id == 0x12345678, "setup: stream ID from the provider");
	TEST_ASSERT_EQ(resp.payload_bytes, MAX_PAYLOAD, "setup: payload_bytes from the provider");
	TEST_ASSERT(resp.src_ip == 0x7f000001, "setup: source IP from the provider");
	TEST_ASSERT_EQ(resp.src_port, 25102, "setup: source port from the provider");
	expect_calls("setup 8960\n", "setup: setup()");

	ret = setup(buf, MAX_PAYLOAD, &resp);
	TEST_ASSERT_EQ(ret, 0, "setup again before the enable");
	expect_calls("setup 8960 pending\n", "setup again: setup(), the first one still pending");

	/* The server cannot enable a buffer without a block */
	block = iiod_client_create_block(buf, BLOCK_SIZE, &data);
	ret = iio_err(block);
	TEST_ASSERT_EQ(ret, 0, "block created");
	if (ret)
		return;

	ret = enable(buf, true);
	TEST_ASSERT_EQ(ret, 0, "enable");
	expect_calls("start\n", "enable: start()");

	ret = enable(buf, true);
	TEST_ASSERT_EQ(ret, 0, "enable again");
	expect_calls("", "enable again: no start(), the setup is used up");

	ret = setup(buf, MAX_PAYLOAD, &resp);
	TEST_ASSERT_EQ(ret, -EBUSY, "setup while streaming");
	expect_calls("", "setup while streaming: -EBUSY without setup()");

	ret = enable(buf, false);
	TEST_ASSERT_EQ(ret, 0, "disable");
	expect_calls("stop\n", "disable: stop()");

	ret = enable(buf, true);
	TEST_ASSERT_EQ(ret, 0, "enable without a setup");
	expect_calls("", "enable without a setup: no start()");

	ret = enable(buf, false);
	TEST_ASSERT_EQ(ret, 0, "disable, nothing streaming");
	expect_calls("", "disable, nothing streaming: no stop()");

	ret = setup(buf, MAX_PAYLOAD, &resp);
	TEST_ASSERT_EQ(ret, 0, "setup, then a disable");
	ret = enable(buf, false);
	TEST_ASSERT_EQ(ret, 0, "disable with a setup pending");
	ret = enable(buf, true);
	TEST_ASSERT_EQ(ret, 0, "enable after that disable");
	expect_calls("setup 8960\n", "the disable dropped the pending setup: no start()");
	ret = enable(buf, false);
	TEST_ASSERT_EQ(ret, 0, "disable");

	/* A positive code would be taken for the length of a tail */
	ret = setup(buf, SETUP_RETURNS_POSITIVE, &resp);
	TEST_ASSERT_EQ(ret, -EIO, "setup() returning 5 reaches the client as -EIO");
	ret = iiod_client_nop(ec->client);
	TEST_ASSERT_EQ(ret, 0, "connection in step after it");
	expect_calls("setup 4321\n", "setup() returning 5: called once");

	ret = setup(buf, START_FAILS, &resp);
	TEST_ASSERT_EQ(ret, 0, "setup for a start() that fails");
	ret = enable(buf, true);
	TEST_ASSERT_EQ(ret, -EADDRINUSE, "start() failing fails the enable");
	expect_calls("setup 1234\nstart failed\n", "failing start() called once");

	/* No disable in between: that would drop a pending setup anyway */
	ret = enable(buf, true);
	TEST_ASSERT_EQ(ret, 0, "enable after the failed start");
	expect_calls("", "the failed start() used the setup up");

	ret = enable(buf, false);
	TEST_ASSERT_EQ(ret, 0, "disable after the failed start");
	expect_calls("", "no stop() after a failed start()");

	ret = setup(buf, MAX_PAYLOAD, &resp);
	TEST_ASSERT_EQ(ret, 0, "setup before a close");
	ret = enable(buf, true);
	TEST_ASSERT_EQ(ret, 0, "enable before a close");
	expect_calls("setup 8960\nstart\n", "streaming before the close");

	iiod_client_free_block(block);
	iiod_client_close_buffer(buf);
	expect_calls("stop\nrelease\n", "close while streaming: stop(), then release()");

	ret = iiod_client_nop(ec->client);
	TEST_ASSERT_EQ(ret, 0, "connection usable after the close");

	/* A setup that never streamed: release() alone */
	buf = iiod_client_open_buffer(ec->client, ec->client, dev, 0, mask);
	ret = iio_err(buf);
	TEST_ASSERT_EQ(ret, 0, "buffer opened again");
	if (ret)
		return;
	ret = setup(buf, MAX_PAYLOAD, &resp);
	TEST_ASSERT_EQ(ret, 0, "setup, then a close");
	iiod_client_close_buffer(buf);
	expect_calls("setup 8960\nrelease\n", "close with a setup pending: release() alone");

	/* No setup at all: the provider never hears of the buffer */
	buf = iiod_client_open_buffer(ec->client, ec->client, dev, 0, mask);
	ret = iio_err(buf);
	TEST_ASSERT_EQ(ret, 0, "buffer opened, never set up");
	if (ret)
		return;
	iiod_client_close_buffer(buf);
	expect_calls("", "close without a setup: no call");

	/* The connection goes while a stream runs */
	buf = iiod_client_open_buffer(ec->client, ec->client, dev, 0, mask);
	ret = iio_err(buf);
	TEST_ASSERT_EQ(ret, 0, "buffer opened for the disconnect");
	if (ret)
		return;
	ret = setup(buf, MAX_PAYLOAD, &resp);
	TEST_ASSERT_EQ(ret, 0, "setup before the disconnect");
	block = iiod_client_create_block(buf, BLOCK_SIZE, &data);
	ret = iio_err(block);
	TEST_ASSERT_EQ(ret, 0, "block created");
	if (!ret) {
		ret = enable(buf, true);
		TEST_ASSERT_EQ(ret, 0, "enable before the disconnect");
		/* The block lives on the client: free it while there is one */
		iiod_client_free_block(block);
	}
	expect_calls("setup 8960\nstart\n", "streaming before the disconnect");

	emu_client_close(ec);
	free(buf); /* iiod_client_close_buffer() without the CLOSE_BUFFER */
	expect_calls_within("stop\nrelease\n", TEARDOWN_TIMEOUT_MS,
			"lost connection while streaming: stop(), then release()");
}

int main(int argc, char **argv)
{
	struct iio_channels_mask *mask = NULL;
	const struct iio_device *dev;
	struct emu_client ec;
	int ret;

	if (argc != 2) {
		printf("Usage: %s <log>\n", argv[0]);
		return 2;
	}
	log_path = argv[1];

	ret = emu_client_open(&ec);
	if (ret)
		return 1;

	/* As with --udp: a client that checks for it can use the mock */
	TEST_ASSERT_PTR_NOT_NULL(iio_context_find_attr(ec.ctx, "iiod,udp-stream"),
			"the mock provider advertises iiod,udp-stream");

	dev = iio_context_find_device(ec.ctx, EMU_DEVICE);
	TEST_ASSERT_PTR_NOT_NULL(dev, EMU_DEVICE " found");
	if (!dev)
		goto out_close;

	mask = iio_create_channels_mask(iio_device_get_channels_count(dev));
	TEST_ASSERT_PTR_NOT_NULL(mask, "mask created");
	if (!mask)
		goto out_close;

	iio_channel_enable(iio_device_find_channel(dev, "voltage0", false), mask);
	iio_channel_enable(iio_device_find_channel(dev, "voltage1", false), mask);

	/* The emulator truncated the log at its start */
	expect_calls("", "no call before the test");

	contract(&ec, dev, mask);

out_close:
	emu_client_close(&ec);
	if (mask)
		iio_channels_mask_destroy(mask);

	printf("test_udp_provider_contract: %d/%d passed, %d failed\n", test_passed, test_count,
			test_failed);

	return test_failed ? 1 : 0;
}
