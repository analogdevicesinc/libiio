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

#define MY_NAME "iio_genxml"

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
	char *xml;
	struct iio_context *ctx;
	int c, option_index = 0, arg_index = 0;
	enum backend backend = LOCAL;

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
			break;
		case 'x':
			if (backend != LOCAL) {
				ERROR("-x and -n are mutually exclusive\n");
				return EXIT_FAILURE;
			}
			backend = XML;
			arg_index += 2;
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

	if (backend == XML)
		ctx = iio_create_xml_context(argv[arg_index]);
	else if (backend == NETWORK)
		ctx = iio_create_network_context(argv[arg_index]);
	else
		ctx = iio_create_local_context();

	if (!ctx) {
		ERROR("Unable to create IIO context\n");
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
