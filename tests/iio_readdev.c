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

enum backend {
	LOCAL,
	NETWORK,
};

static const struct option options[] = {
	  {"help", no_argument, 0, 'h'},
	  {"network", required_argument, 0, 'n'},
	  {0, 0, 0, 0},
};

static const char *options_descriptions[] = {
	"Show this help and quit.",
	"Use the network backend with the provided hostname.",
};

static void usage(void)
{
	unsigned int i;

	printf("Usage:\n\t" MY_NAME " [-n <hostname>] <iio_device> <trigger> "
			"[<channel> ...]\n\nOptions:\n");
	for (i = 0; options[i].name; i++)
		printf("\t-%c, --%s\n\t\t\t%s\n",
					options[i].val, options[i].name,
					options_descriptions[i]);
}

static struct iio_context *ctx;
static const struct iio_device *dev;

void quit_all(int sig)
{
	if (dev)
		iio_device_close(dev);
	if (ctx)
		iio_context_destroy(ctx);
	exit(sig);
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

int main(int argc, char **argv)
{
	const struct iio_device *trigger;
	unsigned int i, nb_channels;
	int ret, c, option_index = 0, arg_index = 0;
	enum backend backend = LOCAL;

	while ((c = getopt_long(argc, argv, "+hn:",
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
		case '?':
			return EXIT_FAILURE;
		}
	}

	if (arg_index + 2 >= argc) {
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

	trigger = get_device(ctx, argv[arg_index + 2]);
	if (!trigger) {
		iio_context_destroy(ctx);
		return EXIT_FAILURE;
	}

	if (!iio_device_is_trigger(trigger)) {
		ERROR("Specified device is not a trigger\n");
		iio_context_destroy(ctx);
		return EXIT_FAILURE;
	}

	iio_trigger_set_rate(trigger, 100); /* Fixed rate for now */
	iio_device_set_trigger(dev, trigger);

	nb_channels = iio_device_get_channels_count(dev);

	if (argc == arg_index + 3) {
		/* Enable all channels */
		for (i = 0; i < nb_channels; i++)
			iio_channel_enable(iio_device_get_channel(dev, i));
	} else {
		for (i = 0; i < nb_channels; i++) {
			unsigned int j;
			struct iio_channel *ch = iio_device_get_channel(dev, i);
			for (j = arg_index + 3; j < argc; j++) {
				const char *n = iio_channel_get_name(ch);
				if (!strcmp(argv[j], iio_channel_get_id(ch)) ||
						(n && !strcmp(n, argv[j])))
					iio_channel_enable(ch);
			}
		}
	}

	ret = iio_device_open(dev);
	if (ret < 0) {
		ERROR("Unable to open device: %s\n", strerror(-ret));
		iio_context_destroy(ctx);
		return EXIT_FAILURE;
	}

	setbuf(stdout, NULL);

	while (true) {
		char buf[128];
		ssize_t ret = iio_device_read_raw(dev, buf, sizeof(buf));
		if (ret < 0) {
			ERROR("Unable to read data: %s\n", strerror(-ret));
			break;
		}
		fwrite(buf, 1, sizeof(buf), stdout);
	}

	iio_context_destroy(ctx);
	return EXIT_FAILURE;
}
