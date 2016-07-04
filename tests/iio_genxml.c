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

#include <getopt.h>
#include <iio.h>
#include <stdio.h>
#include <string.h>

#ifndef _WIN32
#define _strdup strdup
#endif

#define MY_NAME "iio_genxml"

enum backend {
	LOCAL,
	XML,
	NETWORK,
	AUTO,
};

static const struct option options[] = {
	  {"help", no_argument, 0, 'h'},
	  {"xml", required_argument, 0, 'x'},
	  {"network", required_argument, 0, 'n'},
	  {"uri", required_argument, 0, 'u'},
	  {0, 0, 0, 0},
};

static const char *options_descriptions[] = {
	"Show this help and quit.",
	"Use the XML backend with the provided XML file.",
	"Use the network backend with the provided hostname.",
	"Use the context with the provided URI.",
};

static void usage(void)
{
	unsigned int i;

	printf("Usage:\n\t" MY_NAME " [-x <xml_file>]\n\t"
			MY_NAME " [-u <uri>]\n\t"
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
	int c, option_index = 0, arg_index = 0, xml_index = 0, ip_index = 0;
	int uri_index = 0;
	enum backend backend = LOCAL;

	while ((c = getopt_long(argc, argv, "+hn:x:u:",
					options, &option_index)) != -1) {
		switch (c) {
		case 'h':
			usage();
			return EXIT_SUCCESS;
		case 'n':
			if (backend != LOCAL) {
				fprintf(stderr, "-x and -n are mutually exclusive\n");
				return EXIT_FAILURE;
			}
			backend = NETWORK;
			arg_index += 2;
			ip_index = arg_index;
			break;
		case 'x':
			if (backend != LOCAL) {
				fprintf(stderr, "-x and -n are mutually exclusive\n");
				return EXIT_FAILURE;
			}
			backend = XML;
			arg_index += 2;
			xml_index = arg_index;
			break;
		case 'u':
			arg_index += 2;
			uri_index = arg_index;
			backend = AUTO;
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

	if (backend == AUTO) 
		ctx = iio_create_context_from_uri(argv[uri_index]);
	else if (backend == XML)
		ctx = iio_create_xml_context(argv[xml_index]);
	else if (backend == NETWORK)
		ctx = iio_create_network_context(argv[ip_index]);
	else
		ctx = iio_create_default_context();

	if (!ctx) {
		fprintf(stderr, "Unable to create IIO context\n");
		return EXIT_FAILURE;
	}

	xml = _strdup(iio_context_get_xml(ctx));
	if (!xml) {
		iio_context_destroy(ctx);
		return EXIT_FAILURE;
	}

	printf("XML generated:\n\n%s\n\n", xml);

	iio_context_destroy(ctx);

	ctx = iio_create_xml_context_mem(xml, strlen(xml));
	if (!ctx) {
		fprintf(stderr, "Unable to re-generate context\n");
	} else {
		printf("Context re-creation from generated XML succeeded!\n");
		iio_context_destroy(ctx);
	}
	free(xml);
	return EXIT_SUCCESS;
}
