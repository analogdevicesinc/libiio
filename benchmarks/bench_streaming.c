/* SPDX-License-Identifier: MIT */
/*
 * bench_streaming - sustained buffer streaming throughput and sample rate
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 */

#include "bench_common.h"

#include <iio/iio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Skip this long at the start of the run before counting bytes towards the
 * steady-state throughput figure, so cold-start effects (context/DMA
 * warm-up) don't skew the result. */
#define WARMUP_MS 1000

/* Same "first sampling_frequency attr found" heuristic as bench_attr.c, so
 * we can report the device's configured rate alongside the measured one. */
static double read_configured_sample_rate(struct iio_context *ctx, struct iio_device *dev)
{
	const struct iio_attr *attr;
	char buf[128];
	unsigned int nb_chn, c;

	attr = iio_device_find_attr(dev, "sampling_frequency");
	if (!attr) {
		nb_chn = iio_device_get_channels_count(dev);
		for (c = 0; c < nb_chn; c++) {
			struct iio_channel *chn = iio_device_get_channel(dev, c);

			attr = iio_channel_find_attr(chn, "sampling_frequency");
			if (attr)
				break;
		}
	}

	if (!attr)
		return 0.0;

	if (iio_attr_read_raw(attr, buf, sizeof(buf)) < 0)
		return 0.0;

	return strtod(buf, NULL);
}

int main(int argc, char *argv[])
{
	struct bench_opts opts;
	struct iio_context *ctx;
	struct iio_channels_mask *mask = NULL;
	struct iio_device *dev;
	struct iio_buffer *buffer;
	struct iio_buffer_stream *bs;
	struct iio_block *block;
	struct bench_stats stats;
	double samples[1] = { 0 };
	double t_start, t_now, t_warm_end;
	double configured_rate;
	size_t bytes_since_warmup = 0;
	ssize_t sample_size;
	int ret, exit_code = EXIT_FAILURE;

	bench_parse_opts(argc, argv, &opts);

	ctx = iio_create_context(NULL, opts.uri);
	if (iio_err(ctx)) {
		fprintf(stderr, "iio_create_context failed: %d\n", iio_err(ctx));
		return EXIT_FAILURE;
	}
	bench_detect_board(ctx, &opts);

	dev = bench_find_input_device(ctx, &mask);
	if (!dev) {
		fprintf(stderr, "No input-capable device with a buffer found\n");
		iio_context_destroy(ctx);
		return EXIT_FAILURE;
	}

	sample_size = iio_device_get_sample_size(dev, mask);
	configured_rate = read_configured_sample_rate(ctx, dev);

	buffer = iio_device_get_buffer(dev, 0);
	if (!buffer) {
		fprintf(stderr, "Could not get buffer\n");
		goto out_mask;
	}

	bs = iio_buffer_open(buffer, mask);
	if (iio_err(bs)) {
		fprintf(stderr, "Could not open buffer stream: %d\n", iio_err(bs));
		goto out_mask;
	}

	block = iio_buffer_stream_create_block(bs, opts.block_size);
	if (iio_err(block)) {
		fprintf(stderr, "Could not create block: %d\n", iio_err(block));
		goto out_close;
	}

	ret = iio_buffer_stream_start(bs);
	if (ret) {
		fprintf(stderr, "Could not start stream: %d\n", ret);
		iio_block_destroy(block);
		goto out_close;
	}

	t_start = bench_now_us();
	t_warm_end = t_start + (double)WARMUP_MS * 1000.0;

	for (;;) {
		t_now = bench_now_us();
		if (t_now - t_start >= (double)opts.duration_ms * 1000.0)
			break;

		ret = iio_block_enqueue(block, opts.block_size, false);
		if (ret) {
			fprintf(stderr, "enqueue failed: %d\n", ret);
			break;
		}

		ret = iio_block_dequeue(block, false);
		if (ret) {
			fprintf(stderr, "dequeue failed: %d\n", ret);
			break;
		}

		if (t_now >= t_warm_end)
			bytes_since_warmup += opts.block_size;
	}

	t_now = bench_now_us();

	iio_buffer_stream_stop(bs);
	iio_block_destroy(block);

	if (t_now > t_warm_end) {
		double elapsed_s = (t_now - t_warm_end) / 1e6;
		double mib_per_s = ((double)bytes_since_warmup / (1024.0 * 1024.0)) / elapsed_s;

		samples[0] = mib_per_s;
		bench_compute_stats(samples, 1, &stats);
		bench_report(&opts, "streaming_throughput", "MiB/s", &stats);

		if (sample_size > 0) {
			samples[0] = (double)bytes_since_warmup / (double)sample_size / elapsed_s;
			bench_compute_stats(samples, 1, &stats);
			bench_report(&opts, "streaming_sample_rate", "sps", &stats);
		}

		if (configured_rate > 0.0) {
			samples[0] = configured_rate;
			bench_compute_stats(samples, 1, &stats);
			bench_report(&opts, "streaming_configured_sample_rate", "sps", &stats);
		}

		exit_code = EXIT_SUCCESS;
	} else {
		fprintf(stderr, "Run duration shorter than warm-up period (%d ms); "
				"increase --duration-ms\n", WARMUP_MS);
	}

out_close:
	iio_buffer_close(bs);
out_mask:
	iio_channels_mask_destroy(mask);
	iio_context_destroy(ctx);

	return exit_code;
}
