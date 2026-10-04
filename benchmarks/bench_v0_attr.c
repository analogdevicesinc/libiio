/* SPDX-License-Identifier: MIT */
/*
 * bench_v0_attr - attribute read/write round-trip latency, v0 API. Mirrors
 * bench_attr.c's attr_read/attr_write_driver pair against the v0 libiio API
 * instead, so a single --output file tagged with
 * --tag libiio_version=v0/v1 lets compare.py/report.py diff the two
 * directly.
 *
 * v0 has no unified iio_attr object (iio_device_find_attr()/
 * iio_channel_find_attr() just confirm a name exists and hand back that
 * same string), so finding "sampling_frequency" needs its own walk here
 * rather than sharing bench_common.c's bench_find_attr_by_name() - that
 * helper is v1-API-specific and won't link against a v0 libiio anyway.
 *
 * Built only when CMake finds a v0-shaped libiio - see CMakeLists.txt.
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 */

#include "bench_measure.h"

#include <iio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Either a device-level or a channel-level attribute; exactly one of 'dev'/
 * 'chn' is non-NULL. */
struct v0_attr_ref {
	struct iio_device *dev;
	struct iio_channel *chn;
	const char *name;
};

static int v0_attr_read(const struct v0_attr_ref *ref, char *buf, size_t len)
{
	if (ref->dev)
		return (int)iio_device_attr_read(ref->dev, ref->name, buf, len);
	return (int)iio_channel_attr_read(ref->chn, ref->name, buf, len);
}

static int v0_attr_write(const struct v0_attr_ref *ref, const char *buf)
{
	if (ref->dev)
		return (int)iio_device_attr_write(ref->dev, ref->name, buf);
	return (int)iio_channel_attr_write(ref->chn, ref->name, buf);
}

/* Same "search every device, then every device's channels" heuristic as
 * bench_common.c's bench_find_attr_by_name(), reimplemented against v0's
 * name-based find_attr(). Returns 0 and fills 'out' on success, -1 if not
 * found on any device/channel in the context. */
static int v0_find_attr_by_name(struct iio_context *ctx, const char *name,
				 struct v0_attr_ref *out)
{
	unsigned int nb_dev = iio_context_get_devices_count(ctx);
	unsigned int d;

	for (d = 0; d < nb_dev; d++) {
		struct iio_device *dev = iio_context_get_device(ctx, d);
		unsigned int nb_chn, c;

		if (iio_device_find_attr(dev, name)) {
			out->dev = dev;
			out->chn = NULL;
			out->name = name;
			return 0;
		}

		nb_chn = iio_device_get_channels_count(dev);
		for (c = 0; c < nb_chn; c++) {
			struct iio_channel *chn = iio_device_get_channel(dev, c);

			if (iio_channel_find_attr(chn, name)) {
				out->dev = NULL;
				out->chn = chn;
				out->name = name;
				return 0;
			}
		}
	}

	return -1;
}

/* Read-only, no hardware side effects. */
static int do_attr_read(void *arg, unsigned int i)
{
	const struct v0_attr_ref *ref = arg;
	char buf[4096];
	int ret;

	(void)i;

	ret = v0_attr_read(ref, buf, sizeof(buf));
	if (ret < 0) {
		fprintf(stderr, "attr read of '%s' failed: %d\n", ref->name, ret);
		return -1;
	}

	return 0;
}

static int bench_attr_read(const struct bench_opts *opts, const struct v0_attr_ref *ref,
			    const char *name)
{
	return bench_run_timed(opts, name, "us", opts->iterations, do_attr_read, (void *)ref);
}

struct attr_write_ctx {
	const struct v0_attr_ref *ref;
	char baseline[4096];
};

/* Round-trip-safe: writes back the exact value read as the baseline, so
 * there's no net state change - same approach as bench_attr.c's
 * do_attr_write(). */
static int do_attr_write(void *arg, unsigned int i)
{
	struct attr_write_ctx *c = arg;
	int ret;

	(void)i;

	ret = v0_attr_write(c->ref, c->baseline);
	if (ret < 0) {
		fprintf(stderr, "attr write of '%s' failed: %d\n", c->ref->name, ret);
		return -1;
	}

	return 0;
}

static int bench_attr_write(const struct bench_opts *opts, const struct v0_attr_ref *ref,
			     const char *name)
{
	struct attr_write_ctx c;
	int ret;

	c.ref = ref;

	ret = v0_attr_read(ref, c.baseline, sizeof(c.baseline));
	if (ret < 0) {
		fprintf(stderr, "Initial read of '%s' failed: %d\n", ref->name, ret);
		return -1;
	}

	return bench_run_timed(opts, name, "us", opts->iterations, do_attr_write, &c);
}

int main(int argc, char *argv[])
{
	struct bench_opts opts;
	struct iio_context *ctx;
	struct v0_attr_ref attr;
	int exit_code = EXIT_FAILURE;

	bench_parse_opts(argc, argv, &opts);

	ctx = iio_create_context_from_uri(opts.uri);
	if (!ctx) {
		fprintf(stderr, "iio_create_context_from_uri failed\n");
		return EXIT_FAILURE;
	}

	if (v0_find_attr_by_name(ctx, "sampling_frequency", &attr) < 0) {
		fprintf(stderr, "No sampling_frequency attribute found\n");
		goto out_destroy_ctx;
	}

	if (bench_attr_read(&opts, &attr, "attr_read") < 0)
		goto out_destroy_ctx;

	if (bench_attr_write(&opts, &attr, "attr_write_driver") < 0)
		goto out_destroy_ctx;

	exit_code = EXIT_SUCCESS;

out_destroy_ctx:
	iio_context_destroy(ctx);

	return exit_code;
}
