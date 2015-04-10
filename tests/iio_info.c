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

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MY_NAME "iio_info"

enum backend {
	LOCAL,
	XML,
	NETWORK,
};

static const struct option options[] = {
	  {"help", no_argument, 0, 'h'},
	  {"xml", required_argument, 0, 'x'},
	  {"network", required_argument, 0, 'n'},
	  {0, 0, 0, 0},
};

static const char *options_descriptions[] = {
	"Show this help and quit.",
	"Use the XML backend with the provided XML file.",
	"Use the network backend with the provided hostname.",
};

static void usage(void)
{
	unsigned int i;

	printf("Usage:\n\t" MY_NAME " [-x <xml_file>]\n\t"
			MY_NAME " [-n <hostname>]\n\nOptions:\n");
	for (i = 0; options[i].name; i++)
		printf("\t-%c, --%s\n\t\t\t%s\n",
					options[i].val, options[i].name,
					options_descriptions[i]);
}

int main(int argc, char **argv)
{
	struct iio_context *ctx;
	int c, option_index = 0, arg_index = 0, xml_index = 0, ip_index = 0;
	enum backend backend = LOCAL;
	unsigned int major, minor;
	char git_tag[8];
	int ret;

	while ((c = getopt_long(argc, argv, "+hn:x:",
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
			ip_index = arg_index;
			break;
		case 'x':
			if (backend != LOCAL) {
				ERROR("-x and -n are mutually exclusive\n");
				return EXIT_FAILURE;
			}
			backend = XML;
			arg_index += 2;
			xml_index = arg_index;
			break;
		case '?':
			return EXIT_FAILURE;
		}
	}

	if (arg_index >= argc) {
		fprintf(stderr, "Incorrect number of arguments.\n\n");
		usage();
		return EXIT_FAILURE;
	}

	iio_library_get_version(&major, &minor, git_tag);
	INFO("Library version: %u.%u (git tag: %s)\n", major, minor, git_tag);

	if (backend == XML)
		ctx = iio_create_xml_context(argv[xml_index]);
	else if (backend == NETWORK)
		ctx = iio_create_network_context(argv[ip_index]);
	else
		ctx = iio_create_default_context();

	if (!ctx) {
		ERROR("Unable to create IIO context\n");
		return EXIT_FAILURE;
	}

	INFO("IIO context created with %s backend.\n",
			iio_context_get_name(ctx));

	ret = iio_context_get_version(ctx, &major, &minor, git_tag);
	if (!ret)
		INFO("Backend version: %u.%u (git tag: %s)\n",
				major, minor, git_tag);
	else
		ERROR("Unable to get backend version: %i\n", ret);

	INFO("Backend description string: %s\n",
			iio_context_get_description(ctx));

	unsigned int nb_devices = iio_context_get_devices_count(ctx);
	INFO("IIO context has %u devices:\n", nb_devices);

	unsigned int i;
	for (i = 0; i < nb_devices; i++) {
		const struct iio_device *dev = iio_context_get_device(ctx, i);
		const char *name = iio_device_get_name(dev);
		INFO("\t%s: %s\n", iio_device_get_id(dev), name ? name : "" );

		unsigned int nb_channels = iio_device_get_channels_count(dev);
		INFO("\t\t%u channels found:\n", nb_channels);

		unsigned int j;
		for (j = 0; j < nb_channels; j++) {
			struct iio_channel *ch = iio_device_get_channel(dev, j);
			const char *type_name;

			if (iio_channel_is_output(ch))
				type_name = "output";
			else
				type_name = "input";

			name = iio_channel_get_name(ch);
			INFO("\t\t\t%s: %s (%s)\n",
					iio_channel_get_id(ch),
					name ? name : "", type_name);

			unsigned int nb_attrs = iio_channel_get_attrs_count(ch);
			if (!nb_attrs)
				continue;

			INFO("\t\t\t%u channel-specific attributes found:\n",
					nb_attrs);

			unsigned int k;
			for (k = 0; k < nb_attrs; k++) {
				const char *attr = iio_channel_get_attr(ch, k);
				char buf[1024];
				ret = (int) iio_channel_attr_read(ch,
						attr, buf, 1024);
				if (ret > 0)
					INFO("\t\t\t\tattr %u: %s"
							" value: %s\n", k,
							attr, buf);
				else if (ret == -ENOSYS)
					INFO("\t\t\t\tattr %u: %s\n", k, attr);
				else
					ERROR("Unable to read attribute %s\n",
							attr);
			}
		}

		unsigned int nb_attrs = iio_device_get_attrs_count(dev);
		if (!nb_attrs)
			continue;

		INFO("\t\t%u device-specific attributes found:\n", nb_attrs);
		for (j = 0; j < nb_attrs; j++) {
			const char *attr = iio_device_get_attr(dev, j);
			char buf[1024];
			ret = (int) iio_device_attr_read(dev,
					attr, buf, 1024);
			if (ret > 0)
				INFO("\t\t\t\tattr %u: %s value: %s\n", j,
						attr, buf);
			else if (ret == -ENOSYS)
				INFO("\t\t\t\tattr %u: %s\n", j, attr);
			else
				ERROR("Unable to read attribute: %s\n", attr);
		}

		const struct iio_device *trig;
		ret = iio_device_get_trigger(dev, &trig);
		if (ret == 0) {
			if (trig == NULL) {
				INFO("\t\tNo trigger assigned to device\n");
			} else {
				const char *trig_name = iio_device_get_name(trig);
				INFO("\t\tCurrent trigger: %s(%s)\n", iio_device_get_id(trig),
					trig_name ? trig_name : "");
			}
		}
	}

	iio_context_destroy(ctx);
	return EXIT_SUCCESS;
}
