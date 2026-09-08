// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * iio_ping - Part of the industrial I/O (IIO) utilities
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 * Author: Marius Lucacel <marius.lucacel@analog.com>
 */

#include <errno.h>
#include <getopt.h>
#include <iio/iio.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#define msleep(ms) Sleep(ms)
#else
#include <time.h>
static void msleep(unsigned int ms)
{
	struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L };
	nanosleep(&ts, NULL);
}
#endif

#include "iio_common.h"

#define MY_NAME "iio_ping"
#define MY_OPTS "c:i:"

static const struct option options[] = {
	{ "count", required_argument, NULL, 'c' },
	{ "interval", required_argument, NULL, 'i' },
	{ 0, 0, 0, 0 },
};

static const char *options_descriptions[] = {
	"\t\t[-c <count>] [-i <interval_ms>]",
	"Number of pings to send (default: 4).",
	"Interval between pings in milliseconds (default: 1000).",
};

int main(int argc, char **argv)
{
	char **argw;
	struct iio_context *ctx;
	struct option *opts;
	unsigned int i, count = 4, interval_ms = 1000;
	unsigned int sent = 0, received = 0;
	uint64_t start_us, elapsed_us;
	const char *name;
	int c, ret = EXIT_FAILURE;

	argw = dup_argv(MY_NAME, argc, argv);

	ctx = handle_common_opts(
			MY_NAME, argc, argw, MY_OPTS, options, options_descriptions, NULL, &ret);
	opts = add_common_options(options);
	if (!opts) {
		fprintf(stderr, "Failed to add common options\n");
		return EXIT_FAILURE;
	}

	while ((c = getopt_long(argc, argw, "+" COMMON_OPTIONS MY_OPTS, /* Flawfinder: ignore */
				opts, NULL)) != -1) {
		switch (c) {
		case 'h':
		case 'V':
		case 'u':
		case 'T':
			break;
		case 'S':
		case 'a':
			if (!optarg && argc > optind && argw[optind] != NULL &&
					argw[optind][0] != '-')
				optind++;
			break;
		case 'c':
			count = sanitize_clamp("count", optarg, 1, UINT32_MAX);
			break;
		case 'i':
			interval_ms = sanitize_clamp("interval", optarg, 1, UINT32_MAX);
			break;
		case '?':
			printf("Unknown argument '%c'\n", c);
			return EXIT_FAILURE;
		}
	}
	free(opts);

	if (optind != argc) {
		fprintf(stderr, "Incorrect number of arguments.\n\n");
		usage(MY_NAME, options, options_descriptions);
		return EXIT_FAILURE;
	}

	if (!ctx)
		return ret;

	name = iio_context_get_name(ctx);
	printf("PING %s backend (%s)\n", name, iio_context_get_description(ctx));

	for (i = 0; i < count; i++) {
		start_us = get_time_us();
		ret = iio_context_ping(ctx);
		elapsed_us = get_time_us() - start_us;

		sent++;

		if (ret == 0) {
			printf("Reply from %s: seq=%u time=%llu.%03llu ms\n", name, i,
					(unsigned long long)(elapsed_us / 1000),
					(unsigned long long)(elapsed_us % 1000));
			received++;
		} else {
			char err_str[256];

			iio_strerror(-ret, err_str, sizeof(err_str));
			printf("No reply from %s: seq=%u error=%d (%s)\n", name, i, ret, err_str);
		}

		if (i < count - 1)
			msleep(interval_ms);
	}

	printf("\n--- %s ping statistics ---\n", name);
	printf("%u requests sent, %u received, %.1f%% loss\n", sent, received,
			sent ? ((sent - received) * 100.0) / sent : 0.0);

	iio_context_destroy(ctx);
	free_argw(argc, argw);
	return received == sent ? EXIT_SUCCESS : EXIT_FAILURE;
}
