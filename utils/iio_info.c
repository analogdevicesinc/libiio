// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * iio_info - Part of Industrial I/O (IIO) utilities
 *
 * Copyright (C) 2014-2020 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 * */

#include <errno.h>
#include <getopt.h>
#include <iio/iio.h>
#include <iio/iio-debug.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "iio_common.h"

#define MY_NAME "iio_info"

#ifdef _WIN32
#define snprintf sprintf_s
#endif

static const struct option options[] = {
	{0, 0, 0, 0},
};

static const char *options_descriptions[] = {
	("[-x <xml_file>]\n"
		"\t\t\t\t[-u <uri>]"),
};

static int dev_is_buffer_capable(const struct iio_device *dev)
{
	const struct iio_channel *chn;
	unsigned int i;

	for (i = 0; i < iio_device_get_channels_count(dev); i++) {
		chn = iio_device_get_channel(dev, i);

		if (iio_channel_is_scan_element(chn))
			return true;
	}

	return false;
}

#define MY_OPTS ""

int main(int argc, char **argv)
{
	char **argw, *buf;
	struct iio_context *ctx;
	const struct iio_device *dev, *trig;
	const struct iio_channel *ch;
	const struct iio_data_format *format;
	char sign, repeat[12], err_str[1024];
	const char *key, *value, *name, *label, *type_name;
	unsigned int i, j, k, nb_devices, nb_channels, nb_ctx_attrs, nb_attrs;
	struct iio_channels_mask *mask;
	const struct iio_attr *attr;
	struct iio_buffer *buffer;
	struct option *opts;
	int c, ret = EXIT_FAILURE;

	argw = dup_argv(MY_NAME, argc, argv);

	ctx = handle_common_opts(MY_NAME, argc, argw, MY_OPTS,
				 options, options_descriptions, &ret);
	opts = add_common_options(options);
	if (!opts) {
		fprintf(stderr, "Failed to add common options\n");
		return EXIT_FAILURE;
	}
	while ((c = getopt_long(argc, argw, "+" COMMON_OPTIONS MY_OPTS "s", /* Flawfinder: ignore */
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
			if (!optarg && argc > optind && argw[optind] != NULL
					&& argw[optind][0] != '-')
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

	version(MY_NAME);
	printf("IIO context created with %s backend.\n",
			iio_context_get_name(ctx));

	printf("Backend version: %u.%u (git tag: %s)\n",
	       iio_context_get_version_major(ctx),
	       iio_context_get_version_minor(ctx),
	       iio_context_get_version_tag(ctx));

	printf("Backend description string: %s\n",
			iio_context_get_description(ctx));

	nb_ctx_attrs = iio_context_get_attrs_count(ctx);
	if (nb_ctx_attrs > 0)
		printf("IIO context has %u attributes:\n", nb_ctx_attrs);

	for (i = 0; i < nb_ctx_attrs; i++) {
		attr = iio_context_get_attr(ctx, i);
		key = iio_attr_get_name(attr);
		value = iio_attr_get_static_value(attr);

		printf("\t%s: %s\n", key, value);
	}

	nb_devices = iio_context_get_devices_count(ctx);
	printf("IIO context has %u devices:\n", nb_devices);
	buf = xmalloc(BUF_SIZE, MY_NAME);

	for (i = 0; i < nb_devices; i++) {
		dev = iio_context_get_device(ctx, i);
		name = iio_device_get_name(dev);
		label = iio_device_get_label(dev);

		printf("\t%s:", iio_device_get_id(dev));
		if (name)
			printf(" %s", name);
		if (label)
			printf(" (label: %s)", label);
		if (dev_is_buffer_capable(dev))
			printf(" (buffer capable)");
		printf("\n");

		nb_channels = iio_device_get_channels_count(dev);
		printf("\t\t%u channels found:\n", nb_channels);

		mask = nb_channels ? iio_create_channels_mask(nb_channels) : NULL;

		for (j = 0; j < nb_channels; j++) {
			ch = iio_device_get_channel(dev, j);

			if (mask)
				iio_channel_enable(ch, mask);

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
				format = iio_channel_get_data_format(ch);
				sign = format->is_signed ? 's' : 'u';

				repeat[0] = '\0';

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

			nb_attrs = iio_channel_get_attrs_count(ch);
			if (!nb_attrs)
				continue;

			printf("\t\t\t%u channel-specific attributes found:\n",
					nb_attrs);

			for (k = 0; k < nb_attrs; k++) {
				attr = iio_channel_get_attr(ch, k);
				ret = (int) iio_attr_read_raw(attr, buf, BUF_SIZE);

				printf("\t\t\t\tattr %2u: %s ", k,
				       iio_attr_get_name(attr));

				if (ret > 0)
					printf("value: %s\n", buf);
				else
					ctx_perror(ctx, ret, "");
			}
		}

		nb_attrs = iio_device_get_attrs_count(dev);
		if (nb_attrs) {
			printf("\t\t%u device-specific attributes found:\n",
					nb_attrs);
			for (j = 0; j < nb_attrs; j++) {
				attr = iio_device_get_attr(dev, j);
				ret = (int) iio_attr_read_raw(attr, buf, BUF_SIZE);

				printf("\t\t\t\tattr %2u: %s ",
				       j, iio_attr_get_name(attr));
				if (ret > 0)
					printf("value: %s\n", buf);
				else
					ctx_perror(ctx, ret, "");
			}
		}

		if (mask)
			buffer = iio_device_create_buffer(dev, 0, mask);
		if (mask && !iio_err(buffer)) {
			nb_attrs = iio_buffer_get_attrs_count(buffer);
			if (nb_attrs)
				printf("\t\t%u buffer attributes found:\n", nb_attrs);
			for (j = 0; j < nb_attrs; j++) {
				attr = iio_buffer_get_attr(buffer, j);
				ret = (int) iio_attr_read_raw(attr, buf, BUF_SIZE);

				printf("\t\t\tattr %2u: %s ", j,
				       iio_attr_get_name(attr));
				if (ret > 0)
					printf("Value: %s\n", buf);
				else
					ctx_perror(ctx, ret, "");
			}

			iio_buffer_destroy(buffer);
		}

		nb_attrs = iio_device_get_debug_attrs_count(dev);
		if (nb_attrs) {
			printf("\t\t%u debug attributes found:\n", nb_attrs);
			for (j = 0; j < nb_attrs; j++) {
				attr = iio_device_get_debug_attr(dev, j);

				ret = (int) iio_attr_read_raw(attr, buf, BUF_SIZE);
				printf("\t\t\t\tdebug attr %2u: %s ",
				       j, iio_attr_get_name(attr));
				if (ret > 0)
					printf("value: %s\n", buf);
				else
					ctx_perror(ctx, ret, "");
			}
		}

		trig = iio_device_get_trigger(dev);
		ret = iio_err(trig);
		if (ret == 0) {
			name = iio_device_get_name(trig);
			printf("\t\tCurrent trigger: %s(%s)\n",
					iio_device_get_id(trig),
					name ? name : "");
		} else if (ret == -ENODEV) {
			printf("\t\tNo trigger assigned to device\n");
		} else if (ret == -ENOENT) {
			printf("\t\tNo trigger on this device\n");
		} else if (ret < 0) {
			ctx_perror(ctx, ret, "Unable to get trigger");
		}

		if (mask)
			iio_channels_mask_destroy(mask);
	}

	free_argw(argc, argw);
	free(buf);
	iio_context_destroy(ctx);
	return EXIT_SUCCESS;
}
