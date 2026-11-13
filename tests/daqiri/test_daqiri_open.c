/* SPDX-License-Identifier: MIT */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 *
 * Buffer-open check for run_loopback.sh. The api tests skip quietly when a
 * buffer does not open, so a passing api run alone does not show that
 * buffers work through the URI under test.
 *
 * On the context at TESTS_API_URI, take the first device with an input scan
 * element (as tests/api/test_buffer.c does), enable all of them, open buffer
 * 0 and create one block.
 *
 * Usage: test_daqiri_open [--no-buffer]
 * With --no-buffer, assert instead that no device has an input scan element.
 */

#include <errno.h>
#include <iio/iio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_framework.h"

#define DEFAULT_URI "daqiri:127.0.0.1,data=tcp"
#define BLOCK_SAMPLES 256

static bool is_input_scan_element(const struct iio_channel *chn)
{
	return iio_channel_is_scan_element(chn) && !iio_channel_is_output(chn);
}

static const struct iio_device *find_input_dev(const struct iio_context *ctx)
{
	unsigned int i, c;

	for (i = 0; i < iio_context_get_devices_count(ctx); i++) {
		const struct iio_device *dev = iio_context_get_device(ctx, i);

		for (c = 0; c < iio_device_get_channels_count(dev); c++)
			if (is_input_scan_element(iio_device_get_channel(dev, c)))
				return dev;
	}

	return NULL;
}

static void open_buffer(const struct iio_device *dev)
{
	unsigned int c, nb = iio_device_get_channels_count(dev);
	struct iio_buffer_stream *stream;
	struct iio_channels_mask *mask;
	struct iio_block *block;
	struct iio_buffer *buf;
	ssize_t sample_size;
	int ret;

	mask = iio_create_channels_mask(nb);
	TEST_ASSERT_PTR_NOT_NULL(mask, "channels mask created");
	if (!mask)
		return;

	for (c = 0; c < nb; c++) {
		struct iio_channel *chn = iio_device_get_channel(dev, c);

		if (is_input_scan_element(chn))
			iio_channel_enable(chn, mask);
	}

	buf = iio_device_get_buffer(dev, 0);
	TEST_ASSERT_PTR_NOT_NULL(buf, "device has buffer 0");
	if (!buf)
		goto out_free_mask;

	stream = iio_buffer_open(buf, mask);
	ret = iio_err(stream);
	printf("  %s: iio_buffer_open: %d\n", iio_device_get_name(dev), ret);
	TEST_ASSERT_EQ(ret, 0, "buffer opened");
	if (ret)
		goto out_free_mask;

	sample_size = iio_device_get_sample_size(dev, mask);
	TEST_ASSERT(sample_size > 0, "sample size known");
	if (sample_size > 0) {
		block = iio_buffer_stream_create_block(stream, BLOCK_SAMPLES * (size_t)sample_size);
		ret = iio_err(block);
		TEST_ASSERT_EQ(ret, 0, "block created");
		if (!ret)
			iio_block_destroy(block);
	}

	iio_buffer_close(stream);
out_free_mask:
	iio_channels_mask_destroy(mask);
}

int main(int argc, char **argv)
{
	const char *uri = getenv("TESTS_API_URI");
	const struct iio_device *dev;
	struct iio_context *ctx;
	bool no_buffer;

	if (argc > 2 || (argc == 2 && strcmp(argv[1], "--no-buffer"))) {
		fprintf(stderr, "usage: %s [--no-buffer]\n", argv[0]);
		return 2;
	}

	no_buffer = argc == 2;
	if (!uri || !uri[0])
		uri = DEFAULT_URI;

	ctx = iio_create_context(NULL, uri);
	TEST_ASSERT_EQ(iio_err(ctx), 0, "context created");
	if (iio_err(ctx)) {
		ctx = NULL;
		goto out;
	}

	dev = find_input_dev(ctx);
	if (no_buffer) {
		TEST_ASSERT_PTR_NULL(dev, "no device has an input scan element");
	} else {
		TEST_ASSERT_PTR_NOT_NULL(dev, "a device has an input scan element");
		if (dev)
			open_buffer(dev);
	}

out:
	if (ctx)
		iio_context_destroy(ctx);

	printf("test_daqiri_open (%s%s): %d/%d passed, %d failed\n", uri,
			no_buffer ? ", --no-buffer" : "", test_passed, test_count, test_failed);

	return test_failed || !test_count ? 1 : 0;
}
