// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * iio_genxml - Part of the Industrial I/O (IIO) utilities
 *
 * Copyright (C) 2014 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 * */

#include <getopt.h>
#include <iio/iio.h>
#include <stdio.h>
#include <string.h>

#include "iio_common.h"

#define MY_NAME "iio_genxml"

#ifdef _WIN32
#define snprintf sprintf_s
#endif

static const struct option options[] = {
	{0, 0, 0, 0},
};

static const char *options_descriptions[] = {
	("\t[-x <xml_file>]\n"
		"\t\t\t\t[-u <uri>]\n"
		"\t\t\t\t[-n <hostname>]"),
};

int main(int argc, char **argv)
{
	char **argw, *uri;
	const char *xml;
	struct iio_context *ctx;
	size_t xml_len;
	struct option *opts;
	size_t buf_len;
	int c, ret = EXIT_FAILURE;

	argw = dup_argv(MY_NAME, argc, argv);
	ctx = handle_common_opts(MY_NAME, argc, argw, "",
				 options, options_descriptions, &ret);
	opts = add_common_options(options);
	if (!opts) {
		fprintf(stderr, "Failed to add common options\n");
		return EXIT_FAILURE;
	}
	while ((c = getopt_long(argc, argv, "+" COMMON_OPTIONS,  /* Flawfinder: ignore */
					opts, NULL)) != -1) {
		switch (c) {
		/* All these are handled in the common */
		case 'h':
		case 'V':
		case 'u':
		case 'T':
			break;
		case 'S':
		case 'a':
			if (!optarg && argc > optind && argv[optind] != NULL
					&& argv[optind][0] != '-')
				optind++;
			break;
		case '?':
			printf("Unknown argument '%c'\n", c);
			return EXIT_FAILURE;
		}
	}
	free(opts);

	if (optind != argc) {
		fprintf(stderr, "Incorrect number of arguments.\n\n");
		usage(MY_NAME, options, options_descriptions);
		return EXIT_FAILURE;
	}

	if (!ctx)
		return ret;

	xml = iio_context_get_xml(ctx);
	printf("XML generated:\n\n%s\n\n", xml);

	buf_len = strlen(xml) + 5;
	uri = malloc(buf_len);
	if (!uri) {
		iio_context_destroy(ctx);
		return EXIT_FAILURE;
	}

	snprintf(uri, buf_len, "xml:%s", xml);

	iio_context_destroy(ctx);

	ctx = iio_create_context(NULL, uri);
	if (!ctx) {
		fprintf(stderr, "Unable to re-generate context\n");
	} else {
		printf("Context re-creation from generated XML succeeded!\n");
		iio_context_destroy(ctx);
	}

	free_argw(argc, argw);
	free(uri);

	return EXIT_SUCCESS;
}
