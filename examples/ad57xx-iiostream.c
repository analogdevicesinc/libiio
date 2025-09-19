// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * libiio - AD5791 IIO streaming example
 *
 * Copyright 2024 BayLibre, SAS
 * Author: Axel Haslam <ahaslam@baylibre.com>
 **/

#include "iiostream-common.h"

#include <iio/iio.h>
#include <iio/iio-debug.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <errno.h>


#define MAX_SAMPLE_VAL		((1 << 18) - 1)
#define BLOCK_SIZE		(1024 * 4)
bool stop = false;

static void handle_sig(int sig)
{
	printf("Waiting for process to finish... Got signal %d\n", sig);
	stop = true;
}

int get_step_size(struct iio_device *dev, long long freq)
{
	const struct iio_attr *attr;
	long long ratio;
	int ret;

	attr = iio_device_find_attr(dev, "sampling_frequency");
	if (!attr) {
		printf("could not find sampling_frequency  attr\n");
		return -1;
	}

	ret = iio_attr_read_longlong(attr, &ratio);
	if (ret)
		return -1;

	ratio /= freq;

	return (MAX_SAMPLE_VAL / ratio) * 2;
}


int main (int argc, char **argv)
{
	struct iio_context *ctx;
	struct iio_channel *chn;
	struct iio_buffer *buf;
	struct iio_stream *txstream;
	struct iio_device *dev;
	struct iio_channels_mask *mask;
	const struct iio_attr *attr;
	const struct iio_block *block;
	size_t sample_size;
	bool up = true;
	int ret;
	int i;
	int step;

	signal(SIGINT, handle_sig);

	ctx = iio_create_context(NULL, NULL);
	if (!ctx) {
		printf("create context error\n");
		goto out;
	}

	dev = iio_context_find_device(ctx, "ad5791");
	if (!dev) {
		printf("find device error\n");
		goto out;
	}

	chn = iio_device_find_channel(dev, "voltage0", true);
	if (!ctx) {
		printf("find channel error\n");
		goto out;
	}

	attr = iio_channel_find_attr(chn, "powerdown");
	if (!attr) {
		printf("a could not find power down attr\n");
		goto out;
	}

	ret = iio_attr_write_bool(attr, false);
	if (ret) {
		printf("Power up fail: %d\n", ret);
		goto out;
	}

	mask = iio_create_channels_mask(iio_device_get_channels_count(dev));
	if (!mask) {
		printf("channel mask error\n");
		goto out;
	}

	iio_channel_enable(chn, mask);

	sample_size = iio_device_get_sample_size(dev, mask);

	buf = iio_device_create_buffer(dev, 0, mask);
	if (!ctx) {
		printf("create buffer error\n");
		goto out;
	}

	txstream = iio_buffer_create_stream(buf, 4, BLOCK_SIZE);
	if (!txstream) {
		printf("create stream error\n");
		goto out;
	}

	step = get_step_size(dev, 1000);
	if (step < 0) {
		printf("Fail to get step size\n");
		goto out;
	}

	while (!stop) {
		int32_t *p_dat, *p_end;
		ptrdiff_t p_inc = sample_size;

		block = iio_stream_get_next_block(txstream);
		if (!block) {
			printf("get block error\n");
			goto out;
		}

		p_dat = iio_block_first(block, chn);
		p_end= iio_block_end(block);
		for (;p_dat < p_end; p_dat += 1) {
			if (up)
				i += step;
			else
				i -= step;

			*p_dat = (1 << 20) | i;
			if (i >= 0x3FFFF)
				up = false;
			if (i == 0)
				up = true;
		}
	}


out:
	if (txstream)
		iio_stream_destroy(txstream);

	if (buf)
		iio_buffer_destroy(buf);

	ret = iio_attr_write_bool(attr, true);
	if (ret)
		printf("Power down fail: %d\n", ret);

	if (mask)
		iio_channels_mask_destroy(mask);

	if (ctx)
		iio_context_destroy(ctx);

	return 0;
}
