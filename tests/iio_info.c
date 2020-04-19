/*
 * iio_info - Part of Industrial I/O (IIO) utilities
 *
 * Copyright (C) 2014-2020 Analog Devices, Inc.
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

#include <errno.h>
#include <getopt.h>
#include <iio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "iio_common.h"

#define MY_NAME "iio_info"

#ifdef _WIN32
#define snprintf sprintf_s
#endif

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
	"\t[-x <xml_file>]\n"
		"\t\t\t\t[-n <hostname>]\n"
		"\t\t\t\t[-u <uri>]",
	"Show this help and quit.",
	"Use the XML backend with the provided XML file.",
	"Use the network backend with the provided hostname.",
	"Use the context at the provided URI.",
	"Scan for available backends.",
	"Scan for available contexts and if only one is available use it.",
};

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
	int c, option_index = 0;
	const char *arg_uri = NULL;
	const char *arg_ip = NULL;
	const char *arg_xml = NULL;
	enum backend backend = IIO_LOCAL;
	bool do_scan = false, detect_context = false;
	unsigned int i, major, minor;
	char git_tag[8];
	int ret;

	while ((c = getopt_long(argc, argv, "+hn:x:u:sa",
					options, &option_index)) != -1) {
		switch (c) {
		case 'h':
			usage(MY_NAME, options, options_descriptions);
			return EXIT_SUCCESS;
		case 'n':
			if (backend != IIO_LOCAL) {
				fprintf(stderr, "-x, -n and -u are mutually exclusive\n");
				return EXIT_FAILURE;
			}
			backend = IIO_NETWORK;
			arg_ip = optarg;
			break;
		case 'x':
			if (backend != IIO_LOCAL) {
				fprintf(stderr, "-x, -n and -u are mutually exclusive\n");
				return EXIT_FAILURE;
			}
			backend = IIO_XML;
			arg_xml = optarg;
			break;
		case 's':
			do_scan = true;
			break;
		case 'u':
			if (backend != IIO_LOCAL) {
				fprintf(stderr, "-x, -n and -u are mutually exclusive\n");
				return EXIT_FAILURE;
			}
			backend = IIO_AUTO;
			arg_uri = optarg;
			break;
		case 'a':
			detect_context = true;
			break;
		case '?':
			return EXIT_FAILURE;
		}
	}

	if (optind != argc) {
		fprintf(stderr, "Incorrect number of arguments.\n\n");
		usage(MY_NAME, options, options_descriptions);
		return EXIT_FAILURE;
	}

	iio_library_get_version(&major, &minor, git_tag);
	printf("Library version: %u.%u (git tag: %s)\n", major, minor, git_tag);

	printf("Compiled with backends:");
	for (i = 0; i < iio_get_backends_count(); i++)
		printf(" %s", iio_get_backend(i));
	printf("\n");

	if (do_scan) {
		autodetect_context(false, NULL, MY_NAME);
		return EXIT_SUCCESS;
	}

	if (detect_context)
		ctx = autodetect_context(true, NULL, MY_NAME);
	else if (backend == IIO_XML)
		ctx = iio_create_xml_context(arg_xml);
	else if (backend == IIO_NETWORK)
		ctx = iio_create_network_context(arg_ip);
	else if (backend == IIO_AUTO)
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

	printf("IIO context created with %s backend.\n",
			iio_context_get_name(ctx));

	ret = iio_context_get_version(ctx, &major, &minor, git_tag);
	if (!ret)
		printf("Backend version: %u.%u (git tag: %s)\n",
				major, minor, git_tag);
	else {
		char err_str[1024];
		iio_strerror(-ret, err_str, sizeof(err_str));
		fprintf(stderr, "Unable to get backend version: %s (%i)\n", err_str, ret);
	}

	printf("Backend description string: %s\n",
			iio_context_get_description(ctx));

	unsigned int nb_ctx_attrs = iio_context_get_attrs_count(ctx);
	if (nb_ctx_attrs > 0)
		printf("IIO context has %u attributes:\n", nb_ctx_attrs);

	for (i = 0; i < nb_ctx_attrs; i++) {
		const char *key, *value;

		ret = iio_context_get_attr(ctx, i, &key, &value);
		if (ret == 0)
			printf("\t%s: %s\n", key, value);
		else {
			char err_str[1024];
			iio_strerror(-ret, err_str, sizeof(err_str));
			fprintf(stderr, "\tUnable to read IIO context attributes: %s (%i)\n",
					err_str, ret);
		}
	}

	unsigned int nb_devices = iio_context_get_devices_count(ctx);
	printf("IIO context has %u devices:\n", nb_devices);
	char *buf = xmalloc(BUF_SIZE, MY_NAME);

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

			if (iio_channel_get_type(ch) == IIO_CHAN_TYPE_UNKNOWN)
				printf(", WARN:iio_channel_get_type()=UNKNOWN");

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
				ret = (int) iio_channel_attr_read(ch,
						attr, buf, BUF_SIZE);

				printf("\t\t\t\tattr %2u: %s ", k, attr);

				if (ret > 0) {
					printf("value: %s\n", buf);
				} else {
					iio_strerror(-ret, buf, BUF_SIZE);
					printf("ERROR: %s (%i)\n", buf, ret);
				}
			}
		}

		unsigned int nb_attrs = iio_device_get_attrs_count(dev);
		if (nb_attrs) {
			printf("\t\t%u device-specific attributes found:\n",
					nb_attrs);
			for (j = 0; j < nb_attrs; j++) {
				const char *attr = iio_device_get_attr(dev, j);
				ret = (int) iio_device_attr_read(dev,
						attr, buf, BUF_SIZE);

				printf("\t\t\t\tattr %2u: %s ",
						j, attr);

				if (ret > 0) {
					printf("value: %s\n", buf);
				} else {
					iio_strerror(-ret, buf, BUF_SIZE);
					printf("ERROR: %s (%i)\n", buf, ret);
				}
			}
		}

		nb_attrs = iio_device_get_buffer_attrs_count(dev);
		if (nb_attrs) {
			printf("\t\t%u buffer-specific attributes found:\n",
					nb_attrs);
			for (j = 0; j < nb_attrs; j++) {
				const char *attr = iio_device_get_buffer_attr(dev, j);
				ret = (int) iio_device_buffer_attr_read(dev,
						attr, buf, BUF_SIZE);

				printf("\t\t\t\tattr %2u: %s ",
						j, attr);

				if (ret > 0) {
					printf("value: %s\n", buf);
				} else {
					iio_strerror(-ret, buf, BUF_SIZE);
					printf("ERROR: %s (%i)\n", buf, ret);
				}
			}
		}

		nb_attrs = iio_device_get_debug_attrs_count(dev);
		if (nb_attrs) {
			printf("\t\t%u debug attributes found:\n", nb_attrs);
			for (j = 0; j < nb_attrs; j++) {
				const char *attr =
					iio_device_get_debug_attr(dev, j);

				ret = (int) iio_device_debug_attr_read(dev,
						attr, buf, BUF_SIZE);
				printf("\t\t\t\tdebug attr %2u: %s ",
						j, attr);
				if (ret > 0) {
					printf("value: %s\n", buf);
				} else {
					iio_strerror(-ret, buf, BUF_SIZE);
					printf("ERROR: %s (%i)\n", buf, ret);
				}
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
		} else if (ret == -ENOENT) {
			printf("\t\tNo trigger on this device\n");
		} else if (ret < 0) {
			iio_strerror(-ret, buf, BUF_SIZE);
			printf("ERROR: checking for trigger : %s (%i)\n", buf, ret);
		}
	}

	free(buf);
	iio_context_destroy(ctx);
	return EXIT_SUCCESS;
}
