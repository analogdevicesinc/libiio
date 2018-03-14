/*
 * iio_adi_dac_overflow_test
 *
 * Copyright (C) 2015 Analog Devices, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
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

struct xflow_pthread_data {
	struct iio_context *ctx;
	const char *device_name;
};

static const struct option options[] = {
	  {"help", no_argument, 0, 'h'},
	  {"network", required_argument, 0, 'n'},
	  {"uri", required_argument, 0, 'u'},
	  {"buffer-size", required_argument, 0, 's'},
	  {"auto", no_argument, 0, 'a'},
	  {0, 0, 0, 0},
};

static const char *options_descriptions[] = {
	"Show this help and quit.",
	"Use the network backend with the provided hostname.",
	"Use the context with the provided URI.",
	"Size of the buffer in sample sets. Default is 1Msample",
	"Scan for available contexts and if only one is available use it.",
};

static void usage(char *argv[])
{
	unsigned int i;

	printf("Usage:\n\t%s [-n <hostname>] [-u <uri>] [ -a ][-s <size>] <iio_device>\n\nOptions:\n", argv[0]);
	for (i = 0; options[i].name; i++)
		printf("\t-%c, --%s\n\t\t\t%s\n",
					options[i].val, options[i].name,
					options_descriptions[i]);
}

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
	iio_device_reg_write(dev, 0x80000088, 0x6);

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
		if (val)
			iio_device_reg_write(dev, 0x80000088, val);
		sleep(1);
	}

	return (void *)0;
}

static struct iio_context *scan(void)
{
	struct iio_scan_context *scan_ctx;
	struct iio_context_info **info;
	struct iio_context *ctx = NULL;
	unsigned int i;
	ssize_t ret;

	scan_ctx = iio_create_scan_context(NULL, 0);
	if (!scan_ctx) {
		fprintf(stderr, "Unable to create scan context\n");
		return NULL;
	}

	ret = iio_scan_context_get_info_list(scan_ctx, &info);
	if (ret < 0) {
		char err_str[1024];
		iio_strerror(-ret, err_str, sizeof(err_str));
		fprintf(stderr, "Scanning for IIO contexts failed: %s\n", err_str);
		goto err_free_ctx;
	}

	if (ret == 0) {
		printf("No IIO context found.\n");
		goto err_free_info_list;
	}

	if (ret == 1) {
		ctx = iio_create_context_from_uri(iio_context_info_get_uri(info[0]));
	} else {
		fprintf(stderr, "Multiple contexts found. Please select one using --uri:\n");

		for (i = 0; i < (size_t) ret; i++) {
			fprintf(stderr, "\t%d: %s [%s]\n", i,
				iio_context_info_get_description(info[i]),
				iio_context_info_get_uri(info[i]));
		}
	}

	err_free_info_list:
	iio_context_info_list_free(info);
	err_free_ctx:
	iio_scan_context_destroy(scan_ctx);

	return ctx;
}

int main(int argc, char **argv)
{
	unsigned int buffer_size = 1024 * 1024;
	int c, option_index = 0;
	const char *arg_uri = NULL;
	const char *arg_ip = NULL;
	unsigned int n_tx = 0, n_rx = 0;
	static struct iio_context *ctx;
	static struct xflow_pthread_data xflow_pthread_data;
	bool scan_for_context = false;
	unsigned int i, nb_channels;
	struct iio_buffer *buffer;
	pthread_t monitor_thread;
	const char *device_name;
	struct iio_device *dev;
	char unit;
	int ret;

	while ((c = getopt_long(argc, argv, "+hn:u:s:a",
					options, &option_index)) != -1) {
		switch (c) {
		case 'h':
			usage(argv);
			return EXIT_SUCCESS;
		case 's':
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
		case 'n':
			arg_ip = optarg;
			break;
		case 'u':
			arg_uri = optarg;
			break;
		case 'a':
			scan_for_context = true;
			break;
		case '?':
			return EXIT_FAILURE;
		}
	}

	if (optind + 1 != argc) {
		fprintf(stderr, "Incorrect number of arguments.\n\n");
		usage(argv);
		return EXIT_FAILURE;
	}

#ifndef _WIN32
	set_handler(SIGHUP, &quit_all);
#endif
	set_handler(SIGINT, &quit_all);
	set_handler(SIGSEGV, &quit_all);
	set_handler(SIGTERM, &quit_all);


	if (scan_for_context)
		ctx = scan();
	else if (arg_uri)
		ctx = iio_create_context_from_uri(arg_uri);
	else if (arg_ip)
		ctx = iio_create_network_context(arg_ip);
	else
		ctx = iio_create_default_context();

	if (!ctx) {
		fprintf(stderr, "Unable to create IIO context\n");
		return EXIT_FAILURE;
	}

	device_name = argv[optind];

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

	return 0;
}
