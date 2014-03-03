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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
	struct iio_context *ctx;
	char *backend = getenv("LIBIIO_BACKEND");

	if (backend && !strcmp(backend, "xml")) {
		if (argc < 2) {
			ERROR("The XML backend requires the XML file to be "
					"passed as argument\n");
			return EXIT_FAILURE;
		}

		INFO("Creating XML IIO context\n");
		ctx = iio_create_xml_context(argv[1]);
	} else if (backend && !strcmp(backend, "network")) {
		if (argc < 2) {
			ERROR("The XML backend requires the hostname to be "
					"passed as argument\n");
			return EXIT_FAILURE;
		}

		INFO("Creating network IIO context\n");
		ctx = iio_create_network_context(argv[1]);
	} else {
		INFO("Creating local IIO context\n");
		ctx = iio_create_local_context();
	}
	if (!ctx)
		return EXIT_FAILURE;

	INFO("IIO context created: %s\n", iio_context_get_name(ctx));

	unsigned int nb_devices = iio_context_get_devices_count(ctx);
	INFO("IIO context has %u devices:\n", nb_devices);

	unsigned int i;
	for (i = 0; i < nb_devices; i++) {
		const struct iio_device *dev = iio_context_get_device(ctx, i);
		const char *name = iio_device_get_name(dev);
		INFO("\t%s: %s\n", iio_device_get_id(dev), name ?: "" );

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
					iio_channel_get_id(ch), name ?: "",
					type_name);

			unsigned int nb_attrs = iio_channel_get_attrs_count(ch);
			if (!nb_attrs)
				continue;

			INFO("\t\t\t%u channel-specific attributes found:\n",
					nb_attrs);

			unsigned int k;
			for (k = 0; k < nb_attrs; k++) {
				const char *attr = iio_channel_get_attr(ch, k);
				char buf[1024];
				ssize_t ret = iio_channel_attr_read(ch,
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
			ssize_t ret = iio_device_attr_read(dev,
					attr, buf, 1024);
			if (ret > 0)
				INFO("\t\t\t\tattr %u: %s value: %s\n", j,
						attr, buf);
			else if (ret == -ENOSYS)
				INFO("\t\t\t\tattr %u: %s\n", j, attr);
			else
				ERROR("Unable to read attribute: %s\n", attr);
		}
	}

	iio_context_destroy(ctx);
	return EXIT_SUCCESS;
}
