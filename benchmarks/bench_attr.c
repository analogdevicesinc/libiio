/* SPDX-License-Identifier: MIT */
/*
 * bench_attr - attribute read/write round-trip latency
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 */

#include "bench_common.h"

#include <iio/iio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Same "first sampling_frequency attr found" heuristic as
 * scratch/fast_checks.c, so this benchmark works against whatever board is
 * plugged in without hardcoding a device/channel name. */
static const struct iio_attr *find_sampling_freq_attr(struct iio_context *ctx)
{
	unsigned int nb_dev = iio_context_get_devices_count(ctx);
	unsigned int d;

	for (d = 0; d < nb_dev; d++) {
		struct iio_device *dev = iio_context_get_device(ctx, d);
		const struct iio_attr *attr = iio_device_find_attr(dev, "sampling_frequency");
		unsigned int nb_chn, c;

		if (attr)
			return attr;

		nb_chn = iio_device_get_channels_count(dev);
		for (c = 0; c < nb_chn; c++) {
			struct iio_channel *chn = iio_device_get_channel(dev, c);

			attr = iio_channel_find_attr(chn, "sampling_frequency");
			if (attr)
				return attr;
		}
	}

	return NULL;
}

/* FMComms2/AD9361-family specific; not present on every board. Same
 * enumeration approach as find_sampling_freq_attr(). */
static const struct iio_attr *find_gain_table_attr(struct iio_context *ctx)
{
	unsigned int nb_dev = iio_context_get_devices_count(ctx);
	unsigned int d;

	for (d = 0; d < nb_dev; d++) {
		struct iio_device *dev = iio_context_get_device(ctx, d);
		const struct iio_attr *attr = iio_device_find_attr(dev, "gain_table_config");

		if (attr)
			return attr;
	}

	return NULL;
}

/* Reads 'attr's current value into 'baseline', then times 'opts->iterations'
 * writes of that same value back (round-trip-safe, no net state change -
 * same approach as scratch/fast_checks.c's test_attribute_roundtrip),
 * reporting the result as 'name'. */
static int bench_attr_write(const struct bench_opts *opts, const struct iio_attr *attr,
			     const char *name, double *samples)
{
	struct bench_stats stats;
	char baseline[4096];
	unsigned int i;
	ssize_t ret;

	ret = iio_attr_read_raw(attr, baseline, sizeof(baseline));
	if (ret < 0) {
		fprintf(stderr, "Initial read of '%s' failed: %zd\n",
			iio_attr_get_name(attr), ret);
		return -1;
	}

	for (i = 0; i < opts->iterations; i++) {
		double t0 = bench_now_us();

		ret = iio_attr_write_raw(attr, baseline, strlen(baseline) + 1);
		samples[i] = bench_now_us() - t0;

		if (ret < 0) {
			fprintf(stderr, "attr write of '%s' failed: %zd\n",
				iio_attr_get_name(attr), ret);
			return -1;
		}
	}

	bench_compute_stats(samples, opts->iterations, &stats);
	bench_report(opts, name, "us", &stats);

	return 0;
}

/* Times 'opts->iterations' reads of 'attr', reporting the result as 'name'.
 * Read-only, no hardware side effects - safe to run at full iteration
 * count even on an attr whose write path is invasive (e.g. gain_table_config
 * re-programs the AD9361's gain table on every write). */
static int bench_attr_read(const struct bench_opts *opts, const struct iio_attr *attr,
			    const char *name, double *samples)
{
	struct bench_stats stats;
	char buf[4096];
	unsigned int i;
	ssize_t ret;

	for (i = 0; i < opts->iterations; i++) {
		double t0 = bench_now_us();

		ret = iio_attr_read_raw(attr, buf, sizeof(buf));
		samples[i] = bench_now_us() - t0;

		if (ret < 0) {
			fprintf(stderr, "attr read of '%s' failed: %zd\n",
				iio_attr_get_name(attr), ret);
			return -1;
		}
	}

	bench_compute_stats(samples, opts->iterations, &stats);
	bench_report(opts, name, "us", &stats);

	return 0;
}

int main(int argc, char *argv[])
{
	struct bench_opts opts;
	struct iio_context *ctx;
	const struct iio_attr *attr, *gain_attr;
	double *samples;
	struct bench_stats stats;
	char baseline[128];
	unsigned int i;
	ssize_t ret;

	bench_parse_opts(argc, argv, &opts);

	ctx = iio_create_context(NULL, opts.uri);
	if (iio_err(ctx)) {
		fprintf(stderr, "iio_create_context failed: %d\n", iio_err(ctx));
		return EXIT_FAILURE;
	}
	bench_detect_board(ctx, &opts);

	attr = find_sampling_freq_attr(ctx);
	if (!attr) {
		fprintf(stderr, "No sampling_frequency attribute found\n");
		iio_context_destroy(ctx);
		return EXIT_FAILURE;
	}

	ret = iio_attr_read_raw(attr, baseline, sizeof(baseline));
	if (ret < 0) {
		fprintf(stderr, "Initial read of '%s' failed: %zd\n",
			iio_attr_get_name(attr), ret);
		iio_context_destroy(ctx);
		return EXIT_FAILURE;
	}

	samples = calloc(opts.iterations, sizeof(*samples));
	if (!samples) {
		fprintf(stderr, "Out of memory\n");
		iio_context_destroy(ctx);
		return EXIT_FAILURE;
	}

	/* Read latency */
	for (i = 0; i < opts.iterations; i++) {
		char buf[128];
		double t0 = bench_now_us();

		ret = iio_attr_read_raw(attr, buf, sizeof(buf));
		samples[i] = bench_now_us() - t0;

		if (ret < 0) {
			fprintf(stderr, "attr read failed: %zd\n", ret);
			free(samples);
			iio_context_destroy(ctx);
			return EXIT_FAILURE;
		}
	}
	bench_compute_stats(samples, opts.iterations, &stats);
	bench_report(&opts, "attr_read", "us", &stats);

	/* Write latency on a typical small driver-backed attr - round-trips
	 * the same value read above, so there's no net change to the
	 * attribute (same approach as scratch/fast_checks.c's
	 * test_attribute_roundtrip, safe on real hardware). */
	if (bench_attr_write(&opts, attr, "attr_write_driver", samples) < 0) {
		free(samples);
		iio_context_destroy(ctx);
		return EXIT_FAILURE;
	}

	/* Read latency on a much larger attr, to isolate payload-size cost
	 * (parsing/copying a big table, framing a large response) from normal
	 * small-attr cost. Read-only - gain_table_config's write path
	 * re-programs real gain-table hardware on every call, too invasive
	 * to hammer opts.iterations times. Not present on every board - skip
	 * rather than fail. */
	gain_attr = find_gain_table_attr(ctx);
	if (gain_attr) {
		if (bench_attr_read(&opts, gain_attr, "attr_read_large", samples) < 0) {
			free(samples);
			iio_context_destroy(ctx);
			return EXIT_FAILURE;
		}
	} else {
		fprintf(stderr, "No gain_table_config attribute found, skipping attr_read_large\n");
	}

	free(samples);
	iio_context_destroy(ctx);

	return EXIT_SUCCESS;
}
