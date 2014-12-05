/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
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

#include "../debug.h"
#include "../iio.h"

#include <getopt.h>
#include <string.h>
#include <signal.h>

#define MY_NAME "iio_info"

#define SAMPLES_PER_READ 256

enum backend {
	LOCAL,
	NETWORK,
};

static const struct option options[] = {
	  {"help", no_argument, 0, 'h'},
	  {"network", required_argument, 0, 'n'},
	  {"trigger", required_argument, 0, 't'},
	  {0, 0, 0, 0},
};

static const char *options_descriptions[] = {
	"Show this help and quit.",
	"Use the network backend with the provided hostname.",
	"Use the specified trigger.",
};

static void usage(void)
{
	unsigned int i;

	printf("Usage:\n\t" MY_NAME " [-n <hostname>] [-t <trigger>] "
			"<iio_device> [<channel> ...]\n\nOptions:\n");
	for (i = 0; options[i].name; i++)
		printf("\t-%c, --%s\n\t\t\t%s\n",
					options[i].val, options[i].name,
					options_descriptions[i]);
}

static struct iio_context *ctx;
struct iio_buffer *buffer;
static const char *trigger_name = NULL;

static bool app_running = true;
static int exit_code = EXIT_SUCCESS;

void quit_all(int sig)
{
	exit_code = sig;
	app_running = false;
}

static void set_handler(int signal, void (*handler)(int))
{
	struct sigaction sig;
	sigaction(signal, NULL, &sig);
	sig.sa_handler = handler;
	sigaction(signal, &sig, NULL);
}

static struct iio_device * get_device(const struct iio_context *ctx,
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

	ERROR("Device %s not found\n", id);
	return NULL;
}

static ssize_t print_sample(const struct iio_channel *chn,
		void *buf, size_t len, void *d)
{
	fwrite(buf, 1, len, stdout);
	return len;
}

int main(int argc, char **argv)
{
	unsigned int i, nb_channels;
	int c, option_index = 0, arg_index = 0;
	enum backend backend = LOCAL;
	struct iio_device *dev;

	while ((c = getopt_long(argc, argv, "+hn:t:",
					options, &option_index)) != -1) {
		switch (c) {
		case 'h':
			usage();
			return EXIT_SUCCESS;
		case 'n':
			if (backend != LOCAL) {
				ERROR("-x and -n are mutually exclusive\n");
				return EXIT_FAILURE;
			}
			backend = NETWORK;
			arg_index += 2;
			break;
		case 't':
			arg_index += 2;
			trigger_name = argv[arg_index];
			break;
		case '?':
			return EXIT_FAILURE;
		}
	}

	if (arg_index + 1 >= argc) {
		fprintf(stderr, "Incorrect number of arguments.\n\n");
		usage();
		return EXIT_FAILURE;
	}

	if (backend == NETWORK)
		ctx = iio_create_network_context(argv[arg_index]);
	else
		ctx = iio_create_local_context();

	if (!ctx) {
		ERROR("Unable to create IIO context\n");
		return EXIT_FAILURE;
	}


	set_handler(SIGHUP, &quit_all);
	set_handler(SIGINT, &quit_all);
	set_handler(SIGSEGV, &quit_all);
	set_handler(SIGTERM, &quit_all);

	dev = get_device(ctx, argv[arg_index + 1]);
	if (!dev) {
		iio_context_destroy(ctx);
		return EXIT_FAILURE;
	}

	if (trigger_name) {
		struct iio_device *trigger = get_device(ctx, trigger_name);
		if (!trigger) {
			iio_context_destroy(ctx);
			return EXIT_FAILURE;
		}

		if (!iio_device_is_trigger(trigger)) {
			ERROR("Specified device is not a trigger\n");
			iio_context_destroy(ctx);
			return EXIT_FAILURE;
		}

		/* Fixed rate for now */
		iio_device_attr_write_longlong(trigger, "frequency", 100);
		iio_device_set_trigger(dev, trigger);
	}

	nb_channels = iio_device_get_channels_count(dev);

	if (argc == arg_index + 2) {
		/* Enable all channels */
		for (i = 0; i < nb_channels; i++)
			iio_channel_enable(iio_device_get_channel(dev, i));
	} else {
		for (i = 0; i < nb_channels; i++) {
			unsigned int j;
			struct iio_channel *ch = iio_device_get_channel(dev, i);
			for (j = arg_index + 2; j < argc; j++) {
				const char *n = iio_channel_get_name(ch);
				if (!strcmp(argv[j], iio_channel_get_id(ch)) ||
						(n && !strcmp(n, argv[j])))
					iio_channel_enable(ch);
			}
		}
	}

	buffer = iio_device_create_buffer(dev, SAMPLES_PER_READ, false);
	if (!buffer) {
		ERROR("Unable to allocate buffer\n");
		iio_context_destroy(ctx);
		return EXIT_FAILURE;
	}

	setbuf(stdout, NULL);

	while (app_running) {
		int ret = iio_buffer_refill(buffer);
		if (ret < 0) {
			ERROR("Unable to refill buffer: %s\n", strerror(-ret));
			break;
		}

		iio_buffer_foreach_sample(buffer, print_sample, NULL);
	}

	iio_buffer_destroy(buffer);
	iio_context_destroy(ctx);
	return exit_code;
}
