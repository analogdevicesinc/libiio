// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * iio_adi_dac_overflow_test - part of the IIO utilities
 *
 * Copyright (C) 2015 Analog Devices, Inc.
 * */

#include <errno.h>
#include <getopt.h>
#include <iio.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../tests/iio_common.h"


#define MY_NAME "iio_adi_xflow_check"

struct xflow_pthread_data {
	struct iio_context *ctx;
	const char *device_name;
};

static const struct option options[] = {
	  {"buffer-size", required_argument, 0, 's'},
	  {"auto", no_argument, 0, 'a'},
	  {0, 0, 0, 0},
};

static const char *options_descriptions[] = {
	"[-n <hostname>] [-u <uri>] [-a ] [-s <size>] <iio_device>",
	"Size of the buffer in sample sets. Default is 1Msample",
	"Scan for available contexts and if only one is available use it.",
};

static bool app_running = true;
static bool device_is_tx;

static void quit_all(int sig)
{
	app_running = false;
}

static void set_handler(int signal_nb, void (*handler)(int))
{
#ifdef _WIN32
	signal(signal_nb, handler);
#else
	struct sigaction sig;
	sigaction(signal_nb, NULL, &sig);
	sig.sa_handler = handler;
	sigaction(signal_nb, &sig, NULL);
#endif
}

static struct iio_device *get_device(const struct iio_context *ctx,
		const char *id)
{

	unsigned int i, nb_devices = iio_context_get_devices_count(ctx);
	struct iio_device *device;

	for (i = 0; i < nb_devices; i++) {
		const char *name;
		device = iio_context_get_device(ctx, i);
		name = iio_device_get_name(device);
		if (name && !strcmp(name, id))
			break;
		if (!strcmp(id, iio_device_get_id(device)))
			break;
	}

	if (i < nb_devices)
		return device;

	fprintf(stderr, "Device %s not found\n", id);
	return NULL;
}


static void *monitor_thread_fn(void *data)
{
	struct xflow_pthread_data *xflow_pthread_data = data;
	struct iio_context *ctx;
	struct iio_device *dev;
	uint32_t val;
	int ret;

	ctx = xflow_pthread_data->ctx;

	dev = get_device(ctx, xflow_pthread_data->device_name);
	if (!dev) {
		fprintf(stderr, "Unable to find IIO device\n");
		return (void *)-1;
	}

	/* Give the main thread a moment to start the DMA */
	sleep(1);

	/* Clear all status bits */
	ret = iio_device_reg_write(dev, 0x80000088, 0x6);
	if (ret) {
		fprintf(stderr, "Failed to clearn DMA status register: %s\n",
				strerror(-ret));
	}

	while (app_running) {
		ret = iio_device_reg_read(dev, 0x80000088, &val);
		if (ret) {
			fprintf(stderr, "Failed to read status register: %s\n",
					strerror(-ret));
			continue;
		}

		if (device_is_tx) {
			if (val & 1)
				fprintf(stderr, "Underflow detected\n");
		} else {
			if (val & 4)
				fprintf(stderr, "Overflow detected\n");
		}

		/* Clear bits */
		if (val) {
			ret = iio_device_reg_write(dev, 0x80000088, val);
			if (ret)
				fprintf(stderr, "Failed to clearn DMA status register: %s\n",
						strerror(-ret));
		}
		sleep(1);
	}

	return (void *)0;
}

#define MY_OPTS "s:a"

int main(int argc, char **argv)
{
	char **argw;
	unsigned int buffer_size = 1024 * 1024;
	int c;
	unsigned int n_tx = 0, n_rx = 0;
	static struct iio_context *ctx;
	static struct xflow_pthread_data xflow_pthread_data;
	unsigned int i, nb_channels;
	struct iio_buffer *buffer;
	pthread_t monitor_thread;
	const char *device_name;
	struct iio_device *dev;
	char unit;
	int ret = EXIT_FAILURE;
	struct option *opts;

	argw = dup_argv(MY_NAME, argc, argv);

	ctx = handle_common_opts(MY_NAME, argc, argw, MY_OPTS,
				 options, options_descriptions, &ret);
	opts = add_common_options(options);
	if (!opts) {
		fprintf(stderr, "Failed to add common options\n");
		return EXIT_FAILURE;
	}
	while ((c = getopt_long(argc, argw, "+" COMMON_OPTIONS MY_OPTS, /* Flawfinder: ignore */
					opts, NULL)) != -1) {
		switch (c) {
		/* All these are handled in the common */
		case 'h':
		case 'V':
		case 'n':
		case 'x':
		case 'u':
		case 'T':
			break;
		case 'S':
		case 'a':
			if (!optarg && argc > optind && argv[optind] != NULL
					&& argv[optind][0] != '-')
				optind++;
			break;

		case 's':
			if (!optarg) {
				fprintf(stderr, "Samples options requires a value.\n\n");
				return EXIT_FAILURE;
			}
			ret = sscanf(optarg, "%u%c", &buffer_size, &unit);
			if (ret == 0)
				return EXIT_FAILURE;
			if (ret == 2) {
				if (unit == 'k')
					buffer_size *= 1024;
				else if (unit == 'M')
					buffer_size *= 1024 * 1024;
			}
			break;
		case '?':
			return EXIT_FAILURE;
		}
	}
	free(opts);

	if (optind + 1 != argc) {
		fprintf(stderr, "Incorrect number of arguments.\n\n");
		usage(MY_NAME, options, options_descriptions);
		return EXIT_FAILURE;
	}

	if (!ctx)
		return ret;

#ifndef _WIN32
	set_handler(SIGHUP, &quit_all);
#endif
	set_handler(SIGINT, &quit_all);
	set_handler(SIGSEGV, &quit_all);
	set_handler(SIGTERM, &quit_all);

	device_name = argw[optind];

	dev = get_device(ctx, device_name);
	if (!dev) {
		iio_context_destroy(ctx);
		return EXIT_FAILURE;
	}

	nb_channels = iio_device_get_channels_count(dev);
	for (i = 0; i < nb_channels; i++) {
		struct iio_channel *ch = iio_device_get_channel(dev, i);
		if (!iio_channel_is_scan_element(ch))
			continue;
		iio_channel_enable(ch);
		if (iio_channel_is_output(ch))
			n_tx++;
		else
			n_rx++;
	}

	if (n_tx >= n_rx)
		device_is_tx = true;
	else
		device_is_tx = false;

	printf("Monitoring %s for underflows/overflows\n",
		iio_device_get_name(dev));

	buffer = iio_device_create_buffer(dev, buffer_size, false);
	if (!buffer) {
		fprintf(stderr, "Unable to allocate buffer\n");
		iio_context_destroy(ctx);
		return EXIT_FAILURE;
	}

	xflow_pthread_data.ctx = ctx;
	xflow_pthread_data.device_name = device_name;

	ret = pthread_create(&monitor_thread, NULL, monitor_thread_fn,
			     (void *)&xflow_pthread_data);
	if (ret) {
		fprintf(stderr, "Failed to create monitor thread: %s\n",
				strerror(-ret));
	}

	while (app_running) {
		if (device_is_tx) {
			ret = iio_buffer_push(buffer);
			if (ret < 0) {
				fprintf(stderr, "Unable to push buffer: %s\n",
						strerror(-ret));
				app_running = false;
				break;
			}
		} else {
			ret = iio_buffer_refill(buffer);
			if (ret < 0) {
				fprintf(stderr, "Unable to refill buffer: %s\n",
						strerror(-ret));
				app_running = false;
				break;
			}
		}
	}

	pthread_join(monitor_thread, NULL);

	iio_buffer_destroy(buffer);
	iio_context_destroy(ctx);
	free_argw(argc, argw);
	return 0;
}
