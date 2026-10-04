/* SPDX-License-Identifier: MIT */
/*
 * bench_v0_context - context creation/destruction latency, v0 API. Mirrors
 * bench_context.c's context_create/context_destroy pair against the v0
 * libiio API instead, so a single --output file tagged with
 * --tag libiio_version=v0/v1 lets compare.py/report.py diff the two
 * directly.
 *
 * Built only when CMake finds a v0-shaped libiio - see CMakeLists.txt.
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 */

#include "bench_measure.h"

#include <iio.h>
#include <stdio.h>
#include <stdlib.h>

struct create_destroy_ctx {
	const struct bench_opts *opts;
	struct iio_context *ctx;
};

static int do_context_create(void *arg, unsigned int i)
{
	struct create_destroy_ctx *c = arg;

	(void)i;

	c->ctx = iio_create_context_from_uri(c->opts->uri);
	if (!c->ctx) {
		fprintf(stderr, "iio_create_context_from_uri failed\n");
		return -1;
	}

	return 0;
}

static int do_context_destroy(void *arg, unsigned int i)
{
	struct create_destroy_ctx *c = arg;

	(void)i;

	iio_context_destroy(c->ctx);

	return 0;
}

static int bench_create_destroy(const struct bench_opts *opts)
{
	struct create_destroy_ctx c = { .opts = opts };

	return bench_run_paired(opts, "context_create", "context_destroy", "us",
				 opts->iterations, do_context_create, do_context_destroy, &c);
}

static int do_device_enum(void *arg, unsigned int i)
{
	struct iio_context *ctx = arg;
	unsigned int nb_dev = iio_context_get_devices_count(ctx);
	unsigned int d;

	(void)i;

	for (d = 0; d < nb_dev; d++) {
		struct iio_device *dev = iio_context_get_device(ctx, d);

		iio_device_get_channels_count(dev);
	}

	return 0;
}

static int bench_device_enum(const struct bench_opts *opts, struct iio_context *ctx)
{
	return bench_run_timed(opts, "context_device_enum", "us",
				opts->iterations, do_device_enum, ctx);
}

int main(int argc, char *argv[])
{
	struct bench_opts opts;
	struct iio_context *ctx;
	int exit_code = EXIT_FAILURE;

	bench_parse_opts(argc, argv, &opts);

	ctx = iio_create_context_from_uri(opts.uri);
	if (!ctx) {
		fprintf(stderr, "iio_create_context_from_uri failed\n");
		return EXIT_FAILURE;
	}
	iio_context_destroy(ctx);

	if (bench_create_destroy(&opts) < 0)
		return EXIT_FAILURE;

	ctx = iio_create_context_from_uri(opts.uri);
	if (!ctx) {
		fprintf(stderr, "iio_create_context_from_uri failed\n");
		return EXIT_FAILURE;
	}

	if (bench_device_enum(&opts, ctx) < 0)
		goto out_destroy_ctx;

	exit_code = EXIT_SUCCESS;

out_destroy_ctx:
	iio_context_destroy(ctx);

	return exit_code;
}
