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

#include <errno.h>
#include <getopt.h>
#include <iio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MY_NAME "iio_info"

#ifdef _WIN32
#define snprintf sprintf_s
#endif

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
	  {"scan", no_argument, 0, 's'},
	  {"auto", no_argument, 0, 'a'},
	  {0, 0, 0, 0},
};

static const char *options_descriptions[] = {
	"Show this help and quit.",
	"Use the XML backend with the provided XML file.",
	"Use the network backend with the provided hostname.",
	"Use the context at the provided URI.",
	"Scan for available backends.",
	"Scan for available contexts and if only one is available use it.",
};

static void usage(void)
{
	unsigned int i;

	printf("Usage:\n\t" MY_NAME " [-x <xml_file>]\n\t"
			MY_NAME " [-n <hostname>]\n\t"
			MY_NAME " [-u <uri>]\n\nOptions:\n");
	for (i = 0; options[i].name; i++)
		printf("\t-%c, --%s\n\t\t\t%s\n",
					options[i].val, options[i].name,
					options_descriptions[i]);
}

static void scan(void)
{
	struct iio_scan_context *ctx;
	struct iio_context_info **info;
	unsigned int i;
	ssize_t ret;

	ctx = iio_create_scan_context(NULL, 0);
	if (!ctx) {
		fprintf(stderr, "Unable to create scan context\n");
		return;
	}

	ret = iio_scan_context_get_info_list(ctx, &info);
	if (ret < 0) {
		fprintf(stderr, "Unable to scan: %li\n", (long) ret);
		goto err_free_ctx;
	}

	if (ret == 0) {
		printf("No contexts found.\n");
		goto err_free_info_list;
	}

	printf("Available contexts:\n");

	for (i = 0; i < (size_t) ret; i++) {
		printf("\t%d: %s [%s]\n", i,
			iio_context_info_get_description(info[i]),
			iio_context_info_get_uri(info[i]));
	}

err_free_info_list:
	iio_context_info_list_free(info);
err_free_ctx:
	iio_scan_context_destroy(ctx);
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
			fprintf(stderr, "\t%d: %s [%s]\n", i,
				iio_context_info_get_description(info[i]),
				iio_context_info_get_uri(info[i]));
		}
	}

err_free_info_list:
	iio_context_info_list_free(info);
err_free_ctx:
	iio_scan_context_destroy(scan_ctx);

	return ctx;
}

static int dev_is_buffer_capable(const struct iio_device *dev)
{
	unsigned int i;

	for (i = 0; i < iio_device_get_channels_count(dev); i++) {
		struct iio_channel *chn = iio_device_get_channel(dev, i);

		if (iio_channel_is_scan_element(chn))
			return true;
	}

	return false;
}

int main(int argc, char **argv)
{
	struct iio_context *ctx;
	int c, option_index = 0, arg_index = 0, xml_index = 0, ip_index = 0,
	    uri_index = 0;
	enum backend backend = LOCAL;
	bool do_scan = false, detect_context = false;
	unsigned int i, major, minor;
	char git_tag[8];
	int ret;

	while ((c = getopt_long(argc, argv, "+hn:x:u:sa",
					options, &option_index)) != -1) {
		switch (c) {
		case 'h':
			usage();
			return EXIT_SUCCESS;
		case 'n':
			if (backend != LOCAL) {
				fprintf(stderr, "-x, -n and -u are mutually exclusive\n");
				return EXIT_FAILURE;
			}
			backend = NETWORK;
			arg_index += 2;
			ip_index = arg_index;
			break;
		case 'x':
			if (backend != LOCAL) {
				fprintf(stderr, "-x, -n and -u are mutually exclusive\n");
				return EXIT_FAILURE;
			}
			backend = XML;
			arg_index += 2;
			xml_index = arg_index;
			break;
		case 's':
			arg_index += 1;
			do_scan = true;
			break;
		case 'u':
			if (backend != LOCAL) {
				fprintf(stderr, "-x, -n and -u are mutually exclusive\n");
				return EXIT_FAILURE;
			}
			backend = AUTO;
			arg_index += 2;
			uri_index = arg_index;
			break;
		case 'a':
			arg_index += 1;
			detect_context = true;
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
	printf("Library version: %u.%u (git tag: %s)\n", major, minor, git_tag);

	printf("Compiled with backends:");
	for (i = 0; i < iio_get_backends_count(); i++)
		printf(" %s", iio_get_backend(i));
	printf("\n");

	if (do_scan) {
		scan();
		return EXIT_SUCCESS;
	}

	if (detect_context)
		ctx = autodetect_context();
	else if (backend == XML)
		ctx = iio_create_xml_context(argv[xml_index]);
	else if (backend == NETWORK)
		ctx = iio_create_network_context(argv[ip_index]);
	else if (backend == AUTO)
		ctx = iio_create_context_from_uri(argv[uri_index]);
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

	printf("IIO context created with %s backend.\n",
			iio_context_get_name(ctx));

	ret = iio_context_get_version(ctx, &major, &minor, git_tag);
	if (!ret)
		printf("Backend version: %u.%u (git tag: %s)\n",
				major, minor, git_tag);
	else
		fprintf(stderr, "Unable to get backend version: %i\n", ret);

	printf("Backend description string: %s\n",
			iio_context_get_description(ctx));

	unsigned int nb_ctx_attrs = iio_context_get_attrs_count(ctx);
	if (nb_ctx_attrs > 0)
		printf("IIO context has %u attributes:\n", nb_ctx_attrs);

	for (i = 0; i < nb_ctx_attrs; i++) {
		const char *key, *value;

		iio_context_get_attr(ctx, i, &key, &value);
		printf("\t%s: %s\n", key, value);
	}

	unsigned int nb_devices = iio_context_get_devices_count(ctx);
	printf("IIO context has %u devices:\n", nb_devices);

	for (i = 0; i < nb_devices; i++) {
		const struct iio_device *dev = iio_context_get_device(ctx, i);
		const char *name = iio_device_get_name(dev);
		printf("\t%s:", iio_device_get_id(dev));
		if (name)
			printf(" %s", name);
		if (dev_is_buffer_capable(dev))
			printf(" (buffer capable)");
		printf("\n");

		unsigned int nb_channels = iio_device_get_channels_count(dev);
		printf("\t\t%u channels found:\n", nb_channels);

		unsigned int j;
		for (j = 0; j < nb_channels; j++) {
			struct iio_channel *ch = iio_device_get_channel(dev, j);
			const char *type_name;

			if (iio_channel_is_output(ch))
				type_name = "output";
			else
				type_name = "input";

			name = iio_channel_get_name(ch);
			printf("\t\t\t%s: %s (%s",
					iio_channel_get_id(ch),
					name ? name : "", type_name);

			if (iio_channel_is_scan_element(ch)) {
				const struct iio_data_format *format =
					iio_channel_get_data_format(ch);
				char sign = format->is_signed ? 's' : 'u';
				char repeat[8] = "";

				if (format->is_fully_defined)
					sign += 'A' - 'a';

				if (format->repeat > 1)
					snprintf(repeat, sizeof(repeat), "X%u",
						format->repeat);

				printf(", index: %lu, format: %ce:%c%u/%u%s>>%u)\n",
					iio_channel_get_index(ch),
					format->is_be ? 'b' : 'l',
					sign, format->bits,
					format->length, repeat,
					format->shift);
			} else {
				printf(")\n");
			}

			unsigned int nb_attrs = iio_channel_get_attrs_count(ch);
			if (!nb_attrs)
				continue;

			printf("\t\t\t%u channel-specific attributes found:\n",
					nb_attrs);

			unsigned int k;
			for (k = 0; k < nb_attrs; k++) {
				const char *attr = iio_channel_get_attr(ch, k);
				char buf[1024];
				ret = (int) iio_channel_attr_read(ch,
						attr, buf, sizeof(buf));
				if (ret > 0) {
					printf("\t\t\t\tattr %u: %s"
							" value: %s\n", k,
							attr, buf);
				} else if (ret == -ENOSYS) {
					printf("\t\t\t\tattr %u: %s\n",
							k, attr);
				} else {
					iio_strerror(-ret, buf, sizeof(buf));

					fprintf(stderr, "Unable to read attribute %s: %s\n",
							attr, buf);
				}
			}
		}

		unsigned int nb_attrs = iio_device_get_attrs_count(dev);
		if (nb_attrs) {
			printf("\t\t%u device-specific attributes found:\n",
					nb_attrs);
			for (j = 0; j < nb_attrs; j++) {
				const char *attr = iio_device_get_attr(dev, j);
				char buf[1024];
				ret = (int) iio_device_attr_read(dev,
						attr, buf, sizeof(buf));
				if (ret > 0) {
					printf("\t\t\t\tattr %u: %s value: %s"
							"\n", j, attr, buf);
				} else if (ret == -ENOSYS) {
					printf("\t\t\t\tattr %u: %s\n",
							j, attr);
				} else {
					iio_strerror(-ret, buf, sizeof(buf));

					fprintf(stderr, "Unable to read attribute %s: %s\n",
							attr, buf);
				}
			}
		}

		nb_attrs = iio_device_get_debug_attrs_count(dev);
		if (nb_attrs) {
			printf("\t\t%u debug attributes found:\n", nb_attrs);
			for (j = 0; j < nb_attrs; j++) {
				const char *attr =
					iio_device_get_debug_attr(dev, j);
				char buf[1024];

				ret = (int) iio_device_debug_attr_read(dev,
						attr, buf, sizeof(buf));
				if (ret > 0)
					printf("\t\t\t\tdebug attr %u: %s value: %s\n",
							j, attr, buf);
				else if (ret == -ENOSYS)
					printf("\t\t\t\tdebug attr %u: %s\n", j,
							attr);
			}
		}

		const struct iio_device *trig;
		ret = iio_device_get_trigger(dev, &trig);
		if (ret == 0) {
			if (trig == NULL) {
				printf("\t\tNo trigger assigned to device\n");
			} else {
				name = iio_device_get_name(trig);
				printf("\t\tCurrent trigger: %s(%s)\n",
						iio_device_get_id(trig),
						name ? name : "");
			}
		}
	}

	iio_context_destroy(ctx);
	return EXIT_SUCCESS;
}
