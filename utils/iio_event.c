// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * iio_event - Part of the industrial I/O (IIO) utilities
 *
 * Copyright (C) 2023 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include <errno.h>
#include <getopt.h>
#include <iio/iio.h>
#include <iio/iio-debug.h>
#include <signal.h>
#include <stdio.h>

#include "iio_common.h"

#define MY_NAME "iio_event"

static struct iio_event_stream *stream;
static int exit_code = EXIT_FAILURE;

static const struct option options[] = {
	{0, 0, 0, 0},
};

static const char *options_descriptions[] = {
	"<device>\n"
};

static void quit_stream(int sig)
{
	exit_code = sig;

	if (stream)
		iio_event_stream_destroy(stream);
	stream = NULL;
}

static const char * const iio_ev_type_text[] = {
	[IIO_EV_TYPE_THRESH] = "thresh",
	[IIO_EV_TYPE_MAG] = "mag",
	[IIO_EV_TYPE_ROC] = "roc",
	[IIO_EV_TYPE_THRESH_ADAPTIVE] = "thresh_adaptive",
	[IIO_EV_TYPE_MAG_ADAPTIVE] = "mag_adaptive",
	[IIO_EV_TYPE_CHANGE] = "change",
	[IIO_EV_TYPE_MAG_REFERENCED] = "mag_referenced",
	[IIO_EV_TYPE_GESTURE] = "gesture",
};

static const char * const iio_ev_dir_text[] = {
	[IIO_EV_DIR_EITHER] = "either",
	[IIO_EV_DIR_RISING] = "rising",
	[IIO_EV_DIR_FALLING] = "falling",
	[IIO_EV_DIR_SINGLETAP] = "singletap",
	[IIO_EV_DIR_DOUBLETAP] = "doubletap",
};

static void print_event(const struct iio_device *dev,
			const struct iio_event *event)
{
	const struct iio_channel *chn;
	enum iio_event_type type = iio_event_get_type(event);
	enum iio_event_direction dir = iio_event_get_direction(event);

	printf("Event: time: %lld", event->timestamp);

	chn = iio_event_get_channel(event, dev, false);
	if (chn)
		printf(", channel(s): %s", iio_channel_get_id(chn));

	chn = iio_event_get_channel(event, dev, true);
	if (chn)
		printf("-%s", iio_channel_get_id(chn));

	printf(", evtype: %s", iio_ev_type_text[type]);

	if (dir != IIO_EV_DIR_NONE)
		printf(", direction: %s", iio_ev_dir_text[dir]);

	printf("\n");
}

int main(int argc, char **argv)
{
	const struct iio_device *dev;
	struct iio_context *ctx;
	struct iio_event event;
	struct option *opts;
	char **argw, *name;
	int c, ret;

	argw = dup_argv(MY_NAME, argc, argv);
	if (!argw)
		return EXIT_FAILURE;

	ctx = handle_common_opts(MY_NAME, argc, argw, "",
				 options, options_descriptions, &ret);
	opts = add_common_options(options);
	if (!opts) {
		fprintf(stderr, "Failed to add common options\n");
		ret = EXIT_FAILURE;
		goto out_ctx_destroy;
	}
	while ((c = getopt_long(argc, argw, "+" COMMON_OPTIONS, /* Flawfinder: ignore */
			opts, NULL)) != -1) {
		switch (c) {
		/* All these are handled in the common */
		case 'h':
		case 'V':
		case 'u':
		case 'T':
			break;
		case 'S':
		case 'a':
			if (!optarg && argc > optind && argv[optind] != NULL
					&& argv[optind][0] != '-')
				optind++;
			break;
		case '?':
			printf("Unknown argument '%c'\n", c);
			ret = EXIT_FAILURE;
			free(opts);
			goto out_ctx_destroy;
		}
	}
	free(opts);

	if ((argc - optind) != 1) {
		usage(MY_NAME, options, options_descriptions);
		ret = EXIT_FAILURE;
		goto out_ctx_destroy;
	}

	name = cmn_strndup(argw[optind], NAME_MAX);
	dev = iio_context_find_device(ctx, name);
	free(name);
	if (!dev) {
		ctx_err(ctx, "Unable to find device\n");
		ret = EXIT_FAILURE;
		goto out_ctx_destroy;
	}

	stream = iio_device_create_event_stream(dev);
	ret = iio_err(stream);
	if (ret) {
		dev_perror(dev, ret, "Unable to create event stream");
		ret = EXIT_FAILURE;
		goto out_ctx_destroy;
	}

	signal(SIGINT, quit_stream);
	signal(SIGTERM, quit_stream);
#ifndef _WIN32
	signal(SIGHUP, quit_stream);
#endif

	for (;;) {
		ret = iio_event_stream_read(stream, &event, false);
		if (ret == -EINTR)
			break;

		if (ret < 0) {
			dev_perror(dev, ret, "Unable to read event");
			ret = EXIT_FAILURE;
			goto out_stream_destroy;
		}

		print_event(dev, &event);
	}

out_stream_destroy:
	if (stream)
		iio_event_stream_destroy(stream);
out_ctx_destroy:
	if (ctx)
		iio_context_destroy(ctx);
out_free_argw:
	free_argw(argc, argw);
	return ret;
}
