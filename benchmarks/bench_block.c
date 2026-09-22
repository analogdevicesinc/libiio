/* SPDX-License-Identifier: MIT */
/*
 * bench_block - iio_block_enqueue()/iio_block_dequeue() latency and
 * sustained sample rate, using a ring of --num-blocks blocks (default 4)
 * kept in flight at --block-size bytes each (default 4096).
 *
 * With --num-blocks 1, every dequeue has to wait out the full round trip
 * because nothing else is in flight to overlap it. With a ring, one
 * block's transfer can happen while others are queued/being processed, so
 * the default measures the realistic pipelined case real streaming code
 * uses. Sweep --block-size and --num-blocks across separate invocations
 * (see run_all.sh) to see how latency and achieved sample rate scale.
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 */

#include "bench_common.h"

#include <iio/iio.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
	struct bench_opts opts;
	struct iio_context *ctx;
	struct iio_channels_mask *mask = NULL;
	struct iio_device *dev;
	struct iio_buffer *buffer;
	struct iio_buffer_stream *bs;
	struct iio_block **blocks;
	double *enq_samples, *deq_samples;
	struct bench_stats stats;
	struct bench_extra extra;
	ssize_t sample_size;
	double t_start, elapsed_s;
	unsigned int i, b;
	int ret, exit_code = EXIT_FAILURE;

	bench_parse_opts(argc, argv, &opts);

	if (opts.num_blocks < 1) {
		fprintf(stderr, "--num-blocks must be >= 1 (got %u)\n", opts.num_blocks);
		return EXIT_FAILURE;
	}

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

	blocks = calloc(opts.num_blocks, sizeof(*blocks));
	if (!blocks) {
		fprintf(stderr, "Out of memory\n");
		goto out_close;
	}

	/* stream_start() requires at least one block created first; here we
	 * create the whole ring up front, before starting. */
	for (b = 0; b < opts.num_blocks; b++) {
		blocks[b] = iio_buffer_stream_create_block(bs, opts.block_size);
		if (iio_err(blocks[b])) {
			fprintf(stderr, "Could not create block %u: %d\n",
				b, iio_err(blocks[b]));
			goto out_destroy_blocks;
		}
	}

	ret = iio_buffer_stream_start(bs);
	if (ret) {
		fprintf(stderr, "Could not start stream: %d\n", ret);
		goto out_destroy_blocks;
	}

	/* Prime the ring: get every block in flight before timing anything,
	 * so the timed loop below always has num_blocks-1 other transfers
	 * potentially overlapping the one being dequeued. */
	for (b = 0; b < opts.num_blocks; b++) {
		ret = iio_block_enqueue(blocks[b], opts.block_size, false);
		if (ret) {
			fprintf(stderr, "Priming enqueue failed on block %u: %d\n", b, ret);
			goto out_stop;
		}
	}

	enq_samples = calloc(opts.iterations, sizeof(*enq_samples));
	deq_samples = calloc(opts.iterations, sizeof(*deq_samples));
	if (!enq_samples || !deq_samples) {
		fprintf(stderr, "Out of memory\n");
		free(enq_samples);
		free(deq_samples);
		goto out_stop;
	}

	t_start = bench_now_us();

	for (i = 0; i < opts.iterations; i++) {
		struct iio_block *block = blocks[i % opts.num_blocks];
		double t0 = bench_now_us();

		ret = iio_block_dequeue(block, false);
		deq_samples[i] = bench_now_us() - t0;
		if (ret) {
			fprintf(stderr, "dequeue failed at iteration %u: %d\n", i, ret);
			break;
		}

		t0 = bench_now_us();
		ret = iio_block_enqueue(block, opts.block_size, false);
		enq_samples[i] = bench_now_us() - t0;
		if (ret) {
			fprintf(stderr, "enqueue failed at iteration %u: %d\n", i, ret);
			break;
		}
	}

	elapsed_s = (bench_now_us() - t_start) / 1e6;

	if (i > 0) {
		extra.block_size = (long long)opts.block_size;
		extra.ring_depth = (long long)opts.num_blocks;

		bench_compute_stats(enq_samples, i, &stats);
		bench_report_ex(&opts, "block_enqueue", "us", &stats, &extra);

		bench_compute_stats(deq_samples, i, &stats);
		bench_report_ex(&opts, "block_dequeue", "us", &stats, &extra);

		if (sample_size > 0 && elapsed_s > 0.0) {
			double samples[1];

			samples[0] = (double)i * (double)opts.block_size
				     / (double)sample_size / elapsed_s;
			bench_compute_stats(samples, 1, &stats);
			bench_report_ex(&opts, "block_sample_rate", "sps", &stats, &extra);
		}

		exit_code = EXIT_SUCCESS;
	}

	free(enq_samples);
	free(deq_samples);
out_stop:
	iio_buffer_stream_stop(bs);
out_destroy_blocks:
	for (b = 0; b < opts.num_blocks; b++) {
		if (blocks[b] && !iio_err(blocks[b]))
			iio_block_destroy(blocks[b]);
	}
	free(blocks);
out_close:
	iio_buffer_close(bs);
out_mask:
	iio_channels_mask_destroy(mask);
	iio_context_destroy(ctx);

	return exit_code;
}
