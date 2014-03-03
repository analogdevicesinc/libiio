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

#include <string.h>

int main(int argc, char **argv)
{
	char *xml;
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
	} else {
		INFO("Creating local IIO context\n");
		ctx = iio_create_local_context();
	}
	if (!ctx) {
		ERROR("Unable to create local context\n");
		return EXIT_FAILURE;
	}

	xml = iio_context_get_xml(ctx);
	if (!xml) {
		iio_context_destroy(ctx);
		return EXIT_FAILURE;
	}

	INFO("XML generated:\n\n%s\n\n", xml);

	iio_context_destroy(ctx);

	ctx = iio_create_xml_context_mem(xml, strlen(xml));
	if (!ctx) {
		ERROR("Unable to re-generate context\n");
	} else {
		INFO("Context re-creation from generated XML suceeded!\n");
		iio_context_destroy(ctx);
	}
	free(xml);
	return EXIT_SUCCESS;
}
