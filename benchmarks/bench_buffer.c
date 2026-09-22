/* SPDX-License-Identifier: MIT */
/*
 * bench_buffer - iio_buffer_open()/iio_buffer_close() latency, timed and
 * reported separately, and iio_buffer_stream_create_block() latency across
 * block sizes
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 */

#include "bench_common.h"

#include <iio/iio.h>
#include <stdio.h>
#include <stdlib.h>

/* Block sizes swept for the create_block cost, from a small control-plane
 * sized block up to 1 MiB. */
static const struct {
	size_t size;
	const char *label;
} BLOCK_SIZES[] = {
	{ 512, "512B" },
	{ 4096, "4KiB" },
	{ 65536, "64KiB" },
	{ 1048576, "1MiB" },
};

static int bench_open_close(struct iio_buffer *buffer, struct iio_channels_mask *mask,
			     const struct bench_opts *opts)
{
	double *open_samples, *close_samples;
	struct bench_stats stats;
	unsigned int i;

	open_samples = calloc(opts->iterations, sizeof(*open_samples));
	close_samples = calloc(opts->iterations, sizeof(*close_samples));
	if (!open_samples || !close_samples) {
		fprintf(stderr, "Out of memory\n");
		free(open_samples);
		free(close_samples);
		return -1;
	}

	for (i = 0; i < opts->iterations; i++) {
		double t0 = bench_now_us();
		struct iio_buffer_stream *bs = iio_buffer_open(buffer, mask);
		double t1;

		if (iio_err(bs)) {
			fprintf(stderr, "buffer_open failed at iteration %u: %d\n",
				i, iio_err(bs));
			break;
		}
		open_samples[i] = bench_now_us() - t0;

		t1 = bench_now_us();
		iio_buffer_close(bs);
		close_samples[i] = bench_now_us() - t1;
	}

	if (i > 0) {
		bench_compute_stats(open_samples, i, &stats);
		bench_report(opts, "buffer_open", "us", &stats);

		bench_compute_stats(close_samples, i, &stats);
		bench_report(opts, "buffer_close", "us", &stats);
	}

	free(open_samples);
	free(close_samples);
	return i > 0 ? 0 : -1;
}

static int bench_create_block(struct iio_buffer *buffer, struct iio_channels_mask *mask,
			       const struct bench_opts *opts)
{
	double *samples;
	struct bench_stats stats;
	struct iio_buffer_stream *bs;
	unsigned int i, s;
	char name[64];

	samples = calloc(opts->iterations, sizeof(*samples));
	if (!samples) {
		fprintf(stderr, "Out of memory\n");
		return -1;
	}

	for (s = 0; s < sizeof(BLOCK_SIZES) / sizeof(BLOCK_SIZES[0]); s++) {
		bs = iio_buffer_open(buffer, mask);
		if (iio_err(bs)) {
			fprintf(stderr, "buffer_open failed: %d\n", iio_err(bs));
			continue;
		}

		for (i = 0; i < opts->iterations; i++) {
			double t0 = bench_now_us();
			struct iio_block *block =
				iio_buffer_stream_create_block(bs, BLOCK_SIZES[s].size);

			if (iio_err(block)) {
				fprintf(stderr,
					"create_block(%s) failed at iteration %u: %d\n",
					BLOCK_SIZES[s].label, i, iio_err(block));
				break;
			}
			samples[i] = bench_now_us() - t0;

			iio_block_destroy(block);
		}

		if (i > 0) {
			bench_compute_stats(samples, i, &stats);
			snprintf(name, sizeof(name), "block_create_%s", BLOCK_SIZES[s].label);
			bench_report(opts, name, "us", &stats);
		}

		iio_buffer_close(bs);
	}

	free(samples);
	return 0;
}

int main(int argc, char *argv[])
{
	struct bench_opts opts;
	struct iio_context *ctx;
	struct iio_channels_mask *mask = NULL;
	struct iio_device *dev;
	struct iio_buffer *buffer;
	int exit_code = EXIT_FAILURE;

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

	buffer = iio_device_get_buffer(dev, 0);
	if (!buffer) {
		fprintf(stderr, "Could not get buffer\n");
		goto out_mask;
	}

	if (bench_open_close(buffer, mask, &opts) == 0)
		exit_code = EXIT_SUCCESS;

	if (bench_create_block(buffer, mask, &opts) == 0)
		exit_code = EXIT_SUCCESS;

out_mask:
	iio_channels_mask_destroy(mask);
	iio_context_destroy(ctx);

	return exit_code;
}
