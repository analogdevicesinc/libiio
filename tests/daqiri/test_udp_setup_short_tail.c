/* SPDX-License-Identifier: MIT */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 *
 * iiod_client_udp_stream_setup() against a server that breaks the protocol:
 * iiod-emu --udp-mock-provider <log>, asked for a max_payload of 4322, answers
 * the setup with code 12 (a 12-byte response tail follows) and only 5 bytes of
 * that tail, stalls for 2 s, then fails the setup, which sends 8 more bytes.
 *
 * The client gives up after its 1 s timeout, while its reader thread is still
 * in the middle of the tail. The 7 bytes that complete it arrive a second
 * later and must not land in memory the client no longer owns. When the tail
 * lived on the stack of iiod_client_udp_stream_setup(), they overwrote
 * whatever ran there next: here, a large array checked after they arrived
 * (under ASan with detect_stack_use_after_return=1, a stack-use-after-return).
 *
 * Usage: test_udp_setup_short_tail <log>
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

/* What the mock provider does for this max_payload (udp-mock.c) */
#define SETUP_SHORT_TAIL 4322
#define STALL_MS 2000 /* SHORT_TAIL_STALL_MS */

#define TIMEOUT_MS 1000
/* The late bytes come STALL_MS after the setup was sent: wait well past that */
#define SETTLE_MS (STALL_MS + 1000)

/* Covers the frames of the setup call below the caller's, many times over */
#define CANARY_LEN 65536
#define CANARY_BYTE 0xa5

static long ms_since(const struct timespec *t0)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);

	return (long)(now.tv_sec - t0->tv_sec) * 1000 + (now.tv_nsec - t0->tv_nsec) / 1000000;
}

static void sleep_ms(long ms)
{
	struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L };

	if (ms > 0)
		nanosleep(&ts, NULL);
}

static __attribute__((noinline)) int setup_short_tail(struct iiod_client_buffer_pdata *buf)
{
	const struct iiod_udp_stream_req req = {
		.dst_ip = 0x7f000001, /* 127.0.0.1 */
		.dst_port = 25101,
		.max_payload = SETUP_SHORT_TAIL,
	};
	struct iiod_udp_stream_resp resp;

	return iiod_client_udp_stream_setup(buf, &req, &resp);
}

/*
 * Called right after setup_short_tail() returned, from the same caller: its
 * array then occupies the stack where the setup call's frames were. Fill it,
 * wait until <until_ms> after <t0>, when the late bytes are in, and count the
 * bytes that something else wrote meanwhile. Volatile, so that neither the
 * fill nor the check can be optimised away.
 */
static __attribute__((noinline)) size_t stack_overwritten(const struct timespec *t0, long until_ms)
{
	volatile uint8_t canary[CANARY_LEN];
	size_t i, bad = 0;

	for (i = 0; i < CANARY_LEN; i++)
		canary[i] = CANARY_BYTE;

	sleep_ms(until_ms - ms_since(t0));

	for (i = 0; i < CANARY_LEN; i++)
		bad += canary[i] != CANARY_BYTE;

	return bad;
}

static bool log_has(const char *path, const char *line)
{
	char buf[4096];
	size_t n = 0;
	FILE *f;

	f = fopen(path, "r");
	if (f) {
		n = fread(buf, 1, sizeof(buf) - 1, f);
		fclose(f);
	}
	buf[n] = '\0';

	return strstr(buf, line) != NULL;
}

int main(int argc, char **argv)
{
	struct iiod_client_buffer_pdata *buf;
	struct iio_channels_mask *mask = NULL;
	const struct iio_device *dev;
	struct emu_client ec, other;
	struct timespec t0;
	size_t bad;
	long ms;
	int ret;

	if (argc != 2) {
		printf("Usage: %s <log>\n", argv[0]);
		return 2;
	}

	ret = emu_client_open(&ec);
	if (ret)
		return 1;

	/* TEST_ASSERT_EQ() evaluates its first argument twice: go through <ret> */
	ret = iiod_client_set_timeout(ec.client, TIMEOUT_MS);
	TEST_ASSERT_EQ(ret, 0, "client timeout set to 1 s");

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

	buf = iiod_client_open_buffer(ec.client, ec.client, dev, 0, mask);
	ret = iio_err(buf);
	TEST_ASSERT_EQ(ret, 0, "buffer opened");
	if (ret)
		goto out_close;

	clock_gettime(CLOCK_MONOTONIC, &t0);
	ret = setup_short_tail(buf);
	ms = ms_since(&t0);
	TEST_ASSERT(ret == -ETIMEDOUT || ret == -EPROTO,
			"a tail cut short fails the setup with -ETIMEDOUT or -EPROTO");
	if (ret != -ETIMEDOUT && ret != -EPROTO)
		printf("  setup returned %d\n", ret);
	TEST_ASSERT(ms < STALL_MS, "the setup gave up before the rest of the tail came");

	bad = stack_overwritten(&t0, SETTLE_MS);
	TEST_ASSERT_EQ(bad, 0, "the late bytes did not land on the stack");

	TEST_ASSERT(log_has(argv[1], "setup 4322\n"), "the mock answered the setup");

	/* The connection is out of step with the server now: send nothing more
	 * on it, and free the buffer without a CLOSE_BUFFER */
	emu_client_close(&ec);
	free(buf);

	/* The server is fine */
	ret = emu_client_open(&other);
	TEST_ASSERT_EQ(ret, 0, "a new connection");
	if (!ret) {
		ret = iiod_client_nop(other.client);
		TEST_ASSERT_EQ(ret, 0, "NOP on the new connection");
		emu_client_close(&other);
	}

out_close:
	emu_client_close(&ec);
	if (mask)
		iio_channels_mask_destroy(mask);

	printf("test_udp_setup_short_tail: %d/%d passed, %d failed\n", test_passed, test_count,
			test_failed);

	return test_failed ? 1 : 0;
}
