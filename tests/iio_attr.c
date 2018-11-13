/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014, 2017 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 *         Robin Getz <robin.getz@analog.com>
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>

#define MY_NAME "iio_attr"

#ifdef _WIN32
#define snprintf sprintf_s
#else
#define _strdup strdup
#endif

enum backend {
	LOCAL,
	XML,
	AUTO
};

static bool str_match(const char * haystack, char * needle, bool ignore)
{
	bool ret = false;
	int i;
	char *ncpy, *hcpy, *idx, first, last;

	if (!haystack || !needle)
		return false;

	if (!strlen(haystack) || !strlen(needle))
		return false;

	/* '.' means match any */
	if (!strcmp(".", needle) || !strcmp("*", needle))
		return true;

	ncpy = _strdup(needle);
	hcpy = _strdup(haystack);

	if (!ncpy || !hcpy)
		goto eek;

	if (ignore) {
		for (i = 0; hcpy[i]; i++)
			hcpy[i] = tolower(hcpy[i]);
		for (i = 0; ncpy[i]; i++)
			ncpy[i] = tolower(ncpy[i]);
	}

	first = ncpy[0];
	last = ncpy[strlen(ncpy) - 1];

	if (first != '*' && last == '*') {
		/*  'key*'  */
		ret = !strncmp(hcpy, ncpy, strlen(ncpy) - 1);
	} else if ((first == '*') && (last == '*')) {
		/*  '*key*'  */
		ncpy[strlen(ncpy) - 1] = 0;
		ret = strstr(hcpy, &ncpy[1]);
	} else if ((first == '*') && (last != '*')) {
		/*  '*key'  */
		idx = strstr(hcpy, &ncpy[1]);
		if ((idx + strlen(&ncpy[1])) == (hcpy + strlen(hcpy)))
			ret = true;
	} else {
		/*  'key'  */
		ret = !strcmp(hcpy, ncpy);
	}

eek:
	free(ncpy);
	free(hcpy);

	return ret;
}

static struct iio_context * autodetect_context(void)
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
		printf("Using auto-detected IIO context at URI \"%s\"\n",
				iio_context_info_get_uri(info[0]));
		ctx = iio_create_context_from_uri(iio_context_info_get_uri(info[0]));
	} else {
		fprintf(stderr, "Multiple contexts found. Please select one using --uri:\n");
		for (i = 0; i < (size_t) ret; i++) {
			fprintf(stderr, "\t%d: %s [%s]\n",
					i, iio_context_info_get_description(info[i]),
					iio_context_info_get_uri(info[i]));
		}
	}

err_free_info_list:
	iio_context_info_list_free(info);
err_free_ctx:
	iio_scan_context_destroy(scan_ctx);

	return ctx;
}


static void dump_device_attributes(const struct iio_device *dev,
		const char *attr, const char *wbuf, bool quiet)
{
	ssize_t ret;
	char buf[1024];

	if (!wbuf || !quiet) {
		if (!quiet)
			printf("dev '%s', attr '%s', value :",
					iio_device_get_name(dev), attr);
		ret = iio_device_attr_read(dev, attr, buf, sizeof(buf));
		if (ret > 0) {
			if (quiet)
				printf("%s\n", buf);
			else
				printf("'%s'\n", buf);
		} else {
			iio_strerror(-ret, buf, sizeof(buf));
			printf("ERROR: %s (%li)\n", buf, (long)ret);
		}
	}
	if (wbuf) {
		ret = iio_device_attr_write(dev, attr, wbuf);
		if (ret > 0) {
			if (!quiet)
				printf("wrote %li bytes to %s\n", (long)ret, attr);
		} else {
			iio_strerror(-ret, buf, sizeof(buf));
			printf("ERROR: %s (%li) while writing '%s' with '%s'\n",
					buf, (long)ret, attr, wbuf);
		}
		dump_device_attributes(dev, attr, NULL, quiet);
	}
}

static void dump_buffer_attributes(const struct iio_device *dev,
				  const char *attr, const char *wbuf, bool quiet)
{
	ssize_t ret;
	char buf[1024];

	if (!wbuf || !quiet) {
		ret = iio_device_buffer_attr_read(dev, attr, buf, sizeof(buf));

		if (!quiet)
			printf("dev '%s', buffer attr '%s', value :",
					iio_device_get_name(dev), attr);

		if (ret > 0) {
			if (quiet)
				printf("%s\n", buf);
			else
				printf("'%s'\n", buf);
		} else {
			iio_strerror(-ret, buf, sizeof(buf));
			printf("ERROR: %s (%li)\n", buf, (long)ret);
		}
	}

	if (wbuf) {
		ret = iio_device_buffer_attr_write(dev, attr, wbuf);
		if (ret > 0) {
			if (!quiet)
				printf("wrote %li bytes to %s\n", (long)ret, attr);
		} else {
			iio_strerror(-ret, buf, sizeof(buf));
			printf("ERROR: %s (%li) while writing '%s' with '%s'\n",
					buf, (long)ret, attr, wbuf);
		}
		dump_buffer_attributes(dev, attr, NULL, quiet);
	}
}

static void dump_debug_attributes(const struct iio_device *dev,
				  const char *attr, const char *wbuf, bool quiet)
{
	ssize_t ret;
	char buf[1024];

	if (!wbuf || !quiet) {
		ret = iio_device_debug_attr_read(dev, attr, buf, sizeof(buf));

		if (!quiet)
			printf("dev '%s', debug attr '%s', value :",
					iio_device_get_name(dev), attr);

		if (ret > 0) {
			if (quiet)
				printf("%s\n", buf);
			else
				printf("'%s'\n", buf);
		} else {
			iio_strerror(-ret, buf, sizeof(buf));
			printf("ERROR: %s (%li)\n", buf, (long)ret);
		}
	}

	if (wbuf) {
		ret = iio_device_debug_attr_write(dev, attr, wbuf);
		if (ret > 0) {
			if (!quiet)
				printf("wrote %li bytes to %s\n", (long)ret, attr);
		} else {
			iio_strerror(-ret, buf, sizeof(buf));
			printf("ERROR: %s (%li) while writing '%s' with '%s'\n",
					buf, (long)ret, attr, wbuf);
		}
		dump_debug_attributes(dev, attr, NULL, quiet);
	}
}

static void dump_channel_attributes(const struct iio_device *dev,
		struct iio_channel *ch, const char *attr, const char *wbuf, bool quiet)
{
	ssize_t ret;
	char buf[1024];
	const char *type_name;

	if (!wbuf || !quiet) {
		if (iio_channel_is_output(ch))
			type_name = "output";
		else
			type_name = "input";

		ret = iio_channel_attr_read(ch, attr, buf, sizeof(buf));
		if (!quiet)
			printf("dev '%s', channel '%s' (%s), ",
					iio_device_get_name(dev),
					iio_channel_get_id(ch),
					type_name);
		if (iio_channel_get_name(ch) && !quiet)
			printf("id '%s', ", iio_channel_get_name(ch));

		if (!quiet)
			printf("attr '%s', ", attr);

		if (ret > 0) {
			if (quiet)
				printf("%s\n", buf);
			else
				printf("value '%s'\n", buf);
		} else {
			iio_strerror(-ret, buf, sizeof(buf));
			printf("ERROR: %s (%li)\n", buf, (long)ret);
		}
	}
	if (wbuf) {
		ret = iio_channel_attr_write(ch, attr, wbuf);
		if (ret > 0) {
			if (!quiet)
				printf("wrote %li bytes to %s\n", (long)ret, attr);
		} else {
			iio_strerror(-ret, buf, sizeof(buf));
			printf("error %s (%li) while writing '%s' with '%s'\n",
					buf, (long)ret, attr, wbuf);
		}
		dump_channel_attributes(dev, ch, attr, NULL, quiet);
	}
}

static const struct option options[] = {
	/* help */
	{"help", no_argument, 0, 'h'},
	{"ignore-case", no_argument, 0, 'I'},
	{"quiet", no_argument, 0, 'q'},
	/* context connection */
	{"auto", no_argument, 0, 'a'},
	{"uri", required_argument, 0, 'u'},
	/* Channel qualifiers */
	{"input-channel", no_argument, 0, 'i'},
	{"output-channel", no_argument, 0, 'o'},
	{"scan-channel", no_argument, 0, 's'},
	/* Attribute type */
	{"device-attr", no_argument, 0, 'd'},
	{"channel-attr", no_argument, 0, 'c'},
	{"context-attr", no_argument, 0, 'C'},
	{"buffer-attr", no_argument, 0, 'B'},
	{"debug-attr", no_argument, 0, 'D'},
	{0, 0, 0, 0},
};

static const char *options_descriptions[] = {
	/* help */
	"Show this help and quit.",
	"Ignore case distinctions.",
	"Return result only.",
	/* context connection */
	"Use the first context found.",
	"Use the context at the provided URI.",
	/* Channel qualifiers */
	"Filter Input Channels only.",
	"Filter Output Channels only.",
	"Filter Scan Channels only.",
	/* attribute type */
	"Read/Write device attributes",
	"Read/Write channel attributes.",
	"Read IIO context attributes.",
	"Read/Write buffer attributes.",
	"Read/Write debug attributes.",
};

static void usage(void)
{
	unsigned int i, j = 0, k;

	printf("Usage:\n\t" MY_NAME " [OPTION]...\t-d [device] [attr] [value]\n"
		"\t\t\t\t-c [device] [channel] [attr] [value]\n"
		"\t\t\t\t-B [device] [attr] [value]\n"
		"\t\t\t\t-D [device] [attr] [value]\n"
		"\t\t\t\t-C [attr]\nOptions:\n");
	for (i = 0; options[i].name; i++) {
		k = strlen(options[i].name);
		if (k > j)
			j = k;
	}
	j++;
	for (i = 0; options[i].name; i++) {
		printf("\t-%c, --%s%*c: %s\n",
				options[i].val, options[i].name,
				j - (int)strlen(options[i].name), ' ',
				options_descriptions[i]);
		if (i == 3)
			printf("Optional qualifiers:\n");
		if (i == 6)
			printf("Attribute types:\n");
	}
}

int main(int argc, char **argv)
{
	struct iio_context *ctx;
	int c, option_index = 0;
	int device_index = 0, channel_index = 0, attr_index = 0;
	const char *arg_uri = NULL;
	enum backend backend = LOCAL;
	bool detect_context = false, search_device = false, ignore_case = false,
		search_channel = false, search_buffer = false, search_debug = false,
		search_context = false, input_only = false, output_only = false,
		scan_only = false, quiet = false;
	unsigned int i;
	char *wbuf = NULL;

	while ((c = getopt_long(argc, argv, "+hau:CdcBDiosIq",
					options, &option_index)) != -1) {
		switch (c) {
		/* help */
		case 'h':
			usage();
			return EXIT_SUCCESS;
		/* context connection */
		case 'a':
			detect_context = true;
			break;
		case 'u':
			backend = AUTO;
			arg_uri = optarg;
			break;
		/* Attribute type
		 * 'd'evice, 'c'hannel, 'C'ontext, 'B'uffer or 'D'ebug
		 */
		case 'd':
			search_device = true;
			break;
		case 'c':
			search_channel = true;
			break;
		case 'B':
			search_buffer = true;
			break;
		case 'D':
			search_debug = true;
			break;
		case 'C':
			search_context = true;
			break;
		/* Channel qualifiers */
		case 'i':
			input_only = true;
			break;
		case 'o':
			output_only = true;
			break;
		case 's':
			scan_only = true;
			break;
		/* options */
		case 'I':
			ignore_case = true;
			break;
		case 'q':
			quiet = true;
			break;
		case '?':
			printf("Unknown argument '%c'\n", c);
			return EXIT_FAILURE;
		}
	}

	if ((search_device + search_channel + search_context + search_debug + search_buffer) >= 2 ) {
		fprintf(stderr, "The options -d, -c, -C, -B, and -D are exclusive"
				" (can use only one).\n");
		return EXIT_FAILURE;
	}

	if (!(search_device + search_channel + search_context + search_debug + search_buffer)) {
		if (argc == 1) {
			usage();
			return EXIT_SUCCESS;
		}
		fprintf(stderr, "must specify one of -d, -c, -C, -B or -D.\n");
		return EXIT_FAILURE;
	}

	if (search_context) {
		/* -C [IIO_attribute] */
		if (argc >= optind + 1)
			attr_index = optind;
		if (argc >= optind + 2) {
			fprintf(stderr, "Too many options for searching for context attributes\n");
			return EXIT_FAILURE;
		}
	} else if (search_device) {
		/* -d [device] [attr] [value] */
		if (argc >= optind + 1)
			device_index = optind;
		if (argc >= optind + 2)
			attr_index = optind + 1;
		if (argc >= optind + 3)
			wbuf = argv[optind + 2];
		if (argc >= optind + 4) {
			fprintf(stderr, "Too many options for searching for device attributes\n");
			return EXIT_FAILURE;
		}
	} else if (search_channel) {
		/* -c [device] [channel] [attr] [value] */
		if (argc >= optind + 1)
			device_index = optind;
		if (argc >= optind + 2)
			channel_index = optind + 1;
		if (argc >= optind + 3)
			attr_index = optind + 2;
		if (argc >= optind + 4)
			wbuf = argv[optind + 3];
		if (argc >= optind + 5) {
			fprintf(stderr, "Too many options for searching for channel attributes\n");
			return EXIT_FAILURE;
		}
	} else if (search_buffer) {
		/* -B [device] [attribute] [value] */
		if (argc >= optind + 1)
			device_index = optind;
		if (argc >= optind + 2)
			attr_index = optind + 1;
		if (argc >= optind + 3)
			wbuf = argv[optind + 2];
		if (argc >= optind + 4) {
			fprintf(stderr, "Too many options for searching for buffer attributes\n");
			return EXIT_FAILURE;
		}
	} else if (search_debug) {
		/* -D [device] [attribute] [value] */
		if (argc >= optind + 1)
			device_index = optind;
		if (argc >= optind + 2)
			attr_index = optind + 1;
		if (argc >= optind + 3)
			wbuf = argv[optind + 2];
		if (argc >= optind + 4) {
			fprintf(stderr, "Too many options for searching for device attributes\n");
			return EXIT_FAILURE;
		}
	} else {
		fprintf(stderr, "error in application\n");
		return EXIT_FAILURE;
	}

	if (device_index && !argv[device_index])
		return EXIT_FAILURE;
	if (channel_index && !argv[channel_index])
		return EXIT_FAILURE;
	if (attr_index && !argv[attr_index])
		return EXIT_FAILURE;
	if (wbuf && !wbuf)
		return EXIT_FAILURE;
	if (wbuf && ((device_index && (!strcmp(".", argv[device_index]) ||
				        strchr(argv[device_index], '*'))) ||
		     (channel_index && (!strcmp(".", argv[channel_index]) ||
					 strchr(argv[channel_index], '*'))) ||
		     (attr_index && (!strcmp(".", argv[attr_index])  ||
				      strchr(argv[attr_index], '*'))))) {
		printf("can't write value with wildcard match\n");
		return EXIT_FAILURE;
	}

	if (detect_context)
		ctx = autodetect_context();
	else if (backend == AUTO)
		ctx = iio_create_context_from_uri(arg_uri);
	else
		ctx = iio_create_default_context();

	if (!ctx) {
		if (!detect_context) {
			char buf[1024];

			iio_strerror(errno, buf, sizeof(buf));
			fprintf(stderr, "Unable to create IIO context: %s\n",
					buf);
		}

		return EXIT_FAILURE;
	}

	if (search_context) {
		unsigned int nb_ctx_attrs = iio_context_get_attrs_count(ctx);
		if (!attr_index && nb_ctx_attrs > 0)
			printf("IIO context with %u attributes:\n", nb_ctx_attrs);

		for (i = 0; i < nb_ctx_attrs; i++) {
			const char *key, *value;

			iio_context_get_attr(ctx, i, &key, &value);
			if (!attr_index || str_match(key, argv[attr_index], ignore_case)) {
				printf("%s: %s\n", key, value);
			}
		}
	}

	if (search_device || search_channel || search_buffer || search_debug) {
		unsigned int nb_devices = iio_context_get_devices_count(ctx);

		if (!device_index)
			printf("IIO context has %u devices:\n", nb_devices);

		for (i = 0; i < nb_devices; i++) {
			const struct iio_device *dev = iio_context_get_device(ctx, i);
			const char *name = iio_device_get_name(dev);
			unsigned int nb_attrs, nb_channels, j;


			if (device_index && !str_match(name, argv[device_index], ignore_case))
				continue;

			if ((search_device && !device_index) || (search_channel && !device_index) ||
					(search_buffer && !device_index) || (search_debug && !device_index)) {
				printf("\t%s:", iio_device_get_id(dev));
				if (name)
					printf(" %s", name);
				printf(", ");
			}

			if (search_channel && !device_index)
				printf("found %u channels\n", iio_device_get_channels_count(dev));

			nb_channels = iio_device_get_channels_count(dev);
			for (j = 0; j < nb_channels; j++) {
				struct iio_channel *ch;
				const char *type_name;
				unsigned int k, nb_attrs;

				if (!search_channel || !device_index)
					continue;

				ch = iio_device_get_channel(dev, j);

				if (input_only && iio_channel_is_output(ch))
					continue;
				if (output_only && !iio_channel_is_output(ch))
					continue;
				if (scan_only && !iio_channel_is_scan_element(ch))
					continue;

				if (iio_channel_is_output(ch))
					type_name = "output";
				else
					type_name = "input";

				name = iio_channel_get_name(ch);
				if (channel_index &&
						!str_match(iio_channel_get_id(ch),
						argv[channel_index], ignore_case) &&
						(!name || (name &&
						!str_match( name,argv[channel_index], ignore_case))))
					continue;

				if ((!scan_only && !channel_index) ||
				    ( scan_only && iio_channel_is_scan_element(ch))) {
					printf("dev '%s', channel '%s'",
						iio_device_get_name(dev),
						iio_channel_get_id(ch));

					if (name)
						printf(", id '%s'", name);

					printf(" (%s", type_name);

					if (iio_channel_is_scan_element(ch)) {
						const struct iio_data_format *format =
							iio_channel_get_data_format(ch);
						char sign = format->is_signed ? 's' : 'u';
						char repeat[12] = "";

						if (format->is_fully_defined)
							sign += 'A' - 'a';

						if (format->repeat > 1)
							snprintf(repeat, sizeof(repeat), "X%u",
								format->repeat);
						printf(", index: %lu, format: %ce:%c%u/%u%s>>%u)",
								iio_channel_get_index(ch),
								format->is_be ? 'b' : 'l',
								sign, format->bits,
								format->length, repeat,
									format->shift);
						if (scan_only)
							printf("\n");
						else
							printf(", ");
					} else {
						printf("), ");
					}

				}

				nb_attrs = iio_channel_get_attrs_count(ch);
				if (!channel_index)
					printf("found %u channel-specific attributes\n",
							nb_attrs);

				if (!nb_attrs || !channel_index)
					continue;

				for (k = 0; k < nb_attrs; k++) {
					const char *attr =
						iio_channel_get_attr(ch, k);

					if (attr_index &&
						!str_match(attr, argv[attr_index],
							ignore_case))
						continue;

					dump_channel_attributes(dev, ch, attr, wbuf,
								attr_index ? quiet : false);
				}
			}

			nb_attrs = iio_device_get_attrs_count(dev);
			if (search_device && !device_index)
				printf("found %u device attributes\n", nb_attrs);

			if (search_device && device_index && nb_attrs) {
				unsigned int j;
				for (j = 0; j < nb_attrs; j++) {
					const char *attr = iio_device_get_attr(dev, j);

					if (attr_index &&
					    !str_match(attr, argv[attr_index], ignore_case))
						continue;

					dump_device_attributes(dev, attr, wbuf,
							       attr_index ? quiet : false);
				}
			}

			nb_attrs = iio_device_get_buffer_attrs_count(dev);

			if (search_buffer && !device_index)
				printf("found %u buffer attributes\n", nb_attrs);

			if (search_buffer && device_index && nb_attrs) {
				unsigned int j;

				for (j = 0; j < nb_attrs; j++) {
					const char *attr = iio_device_get_buffer_attr(dev, j);

					if ((attr_index && str_match(attr, argv[attr_index],
								ignore_case)) || !attr_index)
						dump_buffer_attributes(dev, attr, wbuf,
									  attr_index ? quiet : false);
				}

			}

			nb_attrs = iio_device_get_debug_attrs_count(dev);

			if (search_debug && !device_index)
				printf("found %u debug attributes\n", nb_attrs);

			if (search_debug && device_index && nb_attrs) {
				unsigned int j;

				for (j = 0; j < nb_attrs; j++) {
					const char *attr = iio_device_get_debug_attr(dev, j);

					if ((attr_index && str_match(attr, argv[attr_index],
								ignore_case)) || !attr_index)
						dump_debug_attributes(dev, attr, wbuf,
								      attr_index ? quiet : false);
				}

			}
		}

	}

	iio_context_destroy(ctx);
	return EXIT_SUCCESS;
}
