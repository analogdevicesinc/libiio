/*
 * iio_genxml - Part of the Industrial I/O (IIO) utilities
 *
 * Copyright (C) 2014 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * */

#include <getopt.h>
#include <iio.h>
#include <stdio.h>
#include <string.h>

#include "iio_common.h"

#ifndef _WIN32
#define _strdup strdup
#endif

#define MY_NAME "iio_genxml"

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
	int c, option_index = 0;
	const char *arg_uri = NULL;
	const char *arg_xml = NULL;
	const char *arg_ip = NULL;
	enum backend backend = IIO_LOCAL;

	while ((c = getopt_long(argc, argv, "+hn:x:u:",
					options, &option_index)) != -1) {
		switch (c) {
		case 'h':
			usage();
			return EXIT_SUCCESS;
		case 'n':
			if (backend != IIO_LOCAL) {
				fprintf(stderr, "-x and -n are mutually exclusive\n");
				return EXIT_FAILURE;
			}
			backend = IIO_NETWORK;
			arg_ip = optarg;
			break;
		case 'x':
			if (backend != IIO_LOCAL) {
				fprintf(stderr, "-x and -n are mutually exclusive\n");
				return EXIT_FAILURE;
			}
			backend = IIO_XML;
			arg_xml = optarg;
			break;
		case 'u':
			arg_uri = optarg;
			backend = IIO_AUTO;
			break;
		case '?':
			return EXIT_FAILURE;
		}
	}

	if (optind != argc) {
		fprintf(stderr, "Incorrect number of arguments.\n\n");
		usage();
		return EXIT_FAILURE;
	}

	if (backend == IIO_AUTO) 
		ctx = iio_create_context_from_uri(arg_uri);
	else if (backend == IIO_XML)
		ctx = iio_create_xml_context(arg_xml);
	else if (backend == IIO_NETWORK)
		ctx = iio_create_network_context(arg_ip);
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
