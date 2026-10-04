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

struct attr_write_ctx {
	const struct iio_attr *attr;
	char baseline[4096];
};

/* Round-trip-safe: writes back the exact value read as the baseline, so
 * there's no net state change (same approach as scratch/fast_checks.c's
 * test_attribute_roundtrip). */
static int do_attr_write(void *arg, unsigned int i)
{
	struct attr_write_ctx *c = arg;
	ssize_t ret;

	(void)i;

	ret = iio_attr_write_raw(c->attr, c->baseline, strlen(c->baseline) + 1);
	if (ret < 0) {
		fprintf(stderr, "attr write of '%s' failed: %zd\n",
			iio_attr_get_name(c->attr), ret);
		return -1;
	}

	return 0;
}

/* Reads 'attr's current value into baseline, then times 'opts->iterations'
 * writes of that same value back, reporting the result as 'name'. */
static int bench_attr_write(const struct bench_opts *opts, const struct iio_attr *attr,
			     const char *name)
{
	struct attr_write_ctx c;
	ssize_t ret;

	c.attr = attr;

	ret = iio_attr_read_raw(attr, c.baseline, sizeof(c.baseline));
	if (ret < 0) {
		fprintf(stderr, "Initial read of '%s' failed: %zd\n",
			iio_attr_get_name(attr), ret);
		return -1;
	}

	return bench_run_timed(opts, name, "us", opts->iterations, do_attr_write, &c);
}

/* Read-only, no hardware side effects - safe to run at full iteration count
 * even on an attr whose write path is invasive (e.g. gain_table_config
 * re-programs the AD9361's gain table on every write). */
static int do_attr_read(void *arg, unsigned int i)
{
	const struct iio_attr *attr = arg;
	char buf[4096];
	ssize_t ret;

	(void)i;

	ret = iio_attr_read_raw(attr, buf, sizeof(buf));
	if (ret < 0) {
		fprintf(stderr, "attr read of '%s' failed: %zd\n",
			iio_attr_get_name(attr), ret);
		return -1;
	}

	return 0;
}

/* Times 'opts->iterations' reads of 'attr', reporting the result as 'name'. */
static int bench_attr_read(const struct bench_opts *opts, const struct iio_attr *attr,
			    const char *name)
{
	return bench_run_timed(opts, name, "us", opts->iterations, do_attr_read, (void *)attr);
}

int main(int argc, char *argv[])
{
	struct bench_opts opts;
	struct iio_context *ctx;
	const struct iio_attr *attr, *gain_attr;
	int exit_code = EXIT_FAILURE;

	bench_parse_opts(argc, argv, &opts);

	ctx = iio_create_context(NULL, opts.uri);
	if (iio_err(ctx)) {
		fprintf(stderr, "iio_create_context failed: %d\n", iio_err(ctx));
		return EXIT_FAILURE;
	}
	bench_detect_board(ctx, &opts);

	attr = bench_find_attr_by_name(ctx, "sampling_frequency");
	if (!attr) {
		fprintf(stderr, "No sampling_frequency attribute found\n");
		goto out_destroy_ctx;
	}

	/* Read latency */
	if (bench_attr_read(&opts, attr, "attr_read") < 0)
		goto out_destroy_ctx;

	/* Write latency on a typical small driver-backed attr. */
	if (bench_attr_write(&opts, attr, "attr_write_driver") < 0)
		goto out_destroy_ctx;

	/* Read latency on a much larger attr, to isolate payload-size cost
	 * (parsing/copying a big table, framing a large response) from normal
	 * small-attr cost. Read-only - gain_table_config's write path
	 * re-programs real gain-table hardware on every call, too invasive
	 * to hammer opts.iterations times. Not present on every board - skip
	 * rather than fail. */
	gain_attr = bench_find_attr_by_name(ctx, "gain_table_config");
	if (gain_attr) {
		if (bench_attr_read(&opts, gain_attr, "attr_read_large") < 0)
			goto out_destroy_ctx;
	} else {
		fprintf(stderr, "No gain_table_config attribute found, skipping attr_read_large\n");
	}

	exit_code = EXIT_SUCCESS;

out_destroy_ctx:
	iio_context_destroy(ctx);

	return exit_code;
}
