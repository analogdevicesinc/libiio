/* SPDX-License-Identifier: MIT */
/*
 * bench_context - context creation and device/attr discovery overhead
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 */

#include "bench_common.h"

#include <iio/iio.h>
#include <stdio.h>
#include <stdlib.h>

static void bench_create_destroy(const struct bench_opts *opts)
{
	double *create_samples = calloc(opts->iterations, sizeof(*create_samples));
	double *destroy_samples = calloc(opts->iterations, sizeof(*destroy_samples));
	struct bench_stats stats;
	unsigned int i;

	if (!create_samples || !destroy_samples) {
		fprintf(stderr, "Out of memory\n");
		free(create_samples);
		free(destroy_samples);
		exit(EXIT_FAILURE);
	}

	for (i = 0; i < opts->iterations; i++) {
		double t0 = bench_now_us();
		struct iio_context *ctx = iio_create_context(NULL, opts->uri);
		double t1;

		create_samples[i] = bench_now_us() - t0;
		if (iio_err(ctx)) {
			fprintf(stderr, "iio_create_context failed: %d\n", iio_err(ctx));
			free(create_samples);
			free(destroy_samples);
			exit(EXIT_FAILURE);
		}

		t1 = bench_now_us();
		iio_context_destroy(ctx);
		destroy_samples[i] = bench_now_us() - t1;
	}

	bench_compute_stats(create_samples, opts->iterations, &stats);
	bench_report(opts, "context_create", "us", &stats);

	bench_compute_stats(destroy_samples, opts->iterations, &stats);
	bench_report(opts, "context_destroy", "us", &stats);

	free(create_samples);
	free(destroy_samples);
}

static void bench_device_enum(const struct bench_opts *opts, struct iio_context *ctx)
{
	double *samples = calloc(opts->iterations, sizeof(*samples));
	struct bench_stats stats;
	unsigned int i;

	if (!samples) {
		fprintf(stderr, "Out of memory\n");
		exit(EXIT_FAILURE);
	}

	for (i = 0; i < opts->iterations; i++) {
		double t0 = bench_now_us();
		unsigned int nb_dev = iio_context_get_devices_count(ctx);
		unsigned int nb_attrs = iio_context_get_attrs_count(ctx);
		unsigned int d;
		double t1;

		for (d = 0; d < nb_dev; d++) {
			struct iio_device *dev = iio_context_get_device(ctx, d);

			iio_device_get_channels_count(dev);
		}
		(void)nb_attrs;

		t1 = bench_now_us();
		samples[i] = t1 - t0;
	}

	bench_compute_stats(samples, opts->iterations, &stats);
	bench_report(opts, "context_device_enum", "us", &stats);
	free(samples);
}

int main(int argc, char *argv[])
{
	struct bench_opts opts;
	struct iio_context *ctx;

	bench_parse_opts(argc, argv, &opts);

	ctx = iio_create_context(NULL, opts.uri);
	if (iio_err(ctx)) {
		fprintf(stderr, "iio_create_context failed: %d\n", iio_err(ctx));
		return EXIT_FAILURE;
	}
	bench_detect_board(ctx, &opts);
	iio_context_destroy(ctx);

	bench_create_destroy(&opts);

	ctx = iio_create_context(NULL, opts.uri);
	if (iio_err(ctx)) {
		fprintf(stderr, "iio_create_context failed: %d\n", iio_err(ctx));
		return EXIT_FAILURE;
	}

	bench_device_enum(&opts, ctx);

	iio_context_destroy(ctx);

	return EXIT_SUCCESS;
}
