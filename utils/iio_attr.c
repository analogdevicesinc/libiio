// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * iio_attr - part of the Industrial I/O (IIO) utilities
 *
 * Copyright (C) 2014 - 2020 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 *         Robin Getz <robin.getz@analog.com>
 * */

#include <errno.h>
#include <getopt.h>
#include <iio/iio.h>
#include <iio/iio-debug.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include "gen_code.h"
#include "iio_common.h"

#define MY_NAME "iio_attr"

#ifdef _WIN32
#define snprintf sprintf_s
#endif

#define IIO_ERR(...) prm_err(NULL, MY_NAME ": " __VA_ARGS__)
#define IIO_PERROR(err, ...) prm_perror(NULL, err, MY_NAME ": " __VA_ARGS__)

enum verbosity {
	ATTR_QUIET,
	ATTR_NORMAL,
	ATTR_VERBOSE,
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

	ncpy = cmn_strndup(needle, NAME_MAX);
	hcpy = cmn_strndup(haystack, NAME_MAX);

	if (!ncpy || !hcpy)
		goto eek;

	if (ignore) {
		for (i = 0; hcpy[i]; i++)
			hcpy[i] = (char) (tolower(hcpy[i]) & 0xFF);
		for (i = 0; ncpy[i]; i++)
			ncpy[i] = (char) (tolower(ncpy[i]) & 0xFF);
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

static inline const char *get_label_or_name_or_id(const struct iio_device *dev)
{
	const char *label, *name;

	label = iio_device_get_label(dev);
	if (label)
		return label;

	name = iio_device_get_name(dev);
	if (name)
		return name;

	return iio_device_get_id(dev);
}

static int dump_device_attributes(const struct iio_device *dev,
				  const struct iio_attr *attr,
				  const char *type, const char *var,
				  const char *wbuf, enum verbosity quiet)
{
	ssize_t ret = 0;
	char *buf = xmalloc(BUF_SIZE, MY_NAME);

	if (!wbuf || quiet == ATTR_VERBOSE) {
		if (quiet == ATTR_VERBOSE) {
			printf("%s ", iio_device_is_trigger(dev) ? "trig" : "dev");
			printf("'%s'", get_label_or_name_or_id(dev));
			printf(", %s attr '%s', value :",
			       type, iio_attr_get_name(attr));
		}
		gen_function(type, var, attr, NULL);
		ret = iio_attr_read_raw(attr, buf, BUF_SIZE);
		if (ret > 0) {
			if (quiet == ATTR_NORMAL)
				printf("%s\n", buf);
			else if (quiet == ATTR_VERBOSE)
				printf("'%s'\n", buf);
		} else {
			IIO_PERROR((int)ret, "Unable to read attribute");
		}
	}
	if (wbuf) {
		gen_function(type, var, attr, wbuf);
		ret = iio_attr_write_string(attr, wbuf);
		if (ret > 0) {
			if (quiet == ATTR_VERBOSE)
				printf("wrote %li bytes to %s\n", (long)ret,
				       iio_attr_get_name(attr));
			dump_device_attributes(dev, attr, type, var, NULL, quiet);
		} else {
			IIO_PERROR((int)ret, "Unable to write attribute");
		}
	}
	free(buf);

	return (int)ret;
}

static int dump_channel_attributes(const struct iio_device *dev,
				   struct iio_channel *ch,
				   const struct iio_attr *attr,
				   const char *wbuf, enum verbosity quiet)
{
	ssize_t ret = 0;
	char *buf = xmalloc(BUF_SIZE, MY_NAME);
	const char *type_name;

	if (!wbuf || quiet == ATTR_VERBOSE) {
		if (iio_channel_is_output(ch))
			type_name = "output";
		else
			type_name = "input";

		gen_function("channel", "ch", attr, NULL);
		ret = iio_attr_read_raw(attr, buf, BUF_SIZE);
		if (quiet == ATTR_VERBOSE) {
			printf("%s ", iio_device_is_trigger(dev) ? "trig" : "dev");
			printf("'%s'", get_label_or_name_or_id(dev));
			printf(", channel '%s' (%s), ",
					iio_channel_get_id(ch),
					type_name);
		}
		if (iio_channel_get_name(ch) && quiet == ATTR_VERBOSE)
			printf("id '%s', ", iio_channel_get_name(ch));

		if (quiet == ATTR_VERBOSE)
			printf("attr '%s', ", iio_attr_get_name(attr));

		if (ret > 0) {
			if (quiet == ATTR_NORMAL)
				printf("%s\n", buf);
			else if (quiet == ATTR_VERBOSE)
				printf("value '%s'\n", buf);
		} else {
			IIO_PERROR((int)ret, "Unable to read channel attribute");
		}
	}
	if (wbuf) {
		gen_function("channel", "ch", attr, wbuf);
		ret = iio_attr_write_string(attr, wbuf);
		if (ret > 0) {
			if (quiet == ATTR_VERBOSE)
				printf("wrote %li bytes to %s\n", (long)ret,
				       iio_attr_get_name(attr));
			dump_channel_attributes(dev, ch, attr, NULL, quiet);
		} else {
			IIO_PERROR((int)ret, "Unable to write channel attribute");
		}
	}
	free(buf);
	return (int)ret;
}

static const struct option options[] = {
	{"ignore-case", no_argument, 0, 'I'},
	{"quiet", no_argument, 0, 'q'},
	{"verbose", no_argument, 0, 'v'},
	{"generate-code", required_argument, 0, 'g'},
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
	("-d [device] [attr] [value]\n"
		"\t\t\t\t-c [device] [channel] [attr] [value]\n"
		"\t\t\t\t-B [device] [attr] [value]\n"
		"\t\t\t\t-D [device] [attr] [value]\n"
		"\t\t\t\t-C [attr]"),
	/* help */
	"Ignore case distinctions.",
	"Return result only.",
	"Verbose, say what is going on",
	"Generate code.",
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

#define MY_OPTS "CdcBDiosIqvg:"
int main(int argc, char **argv)
{
	char **argw;
	struct iio_context *ctx;
	int c, argd = argc;
	int device_index = 0, channel_index = 0, attr_index = 0;
	const char *gen_file = NULL;
	bool search_device = false, ignore_case = false,
		search_channel = false, search_buffer = false, search_debug = false,
		search_context = false, input_only = false, output_only = false,
		scan_only = false, gen_code = false;
	enum verbosity quiet = ATTR_NORMAL;
	bool found_err = false, read_err = false, write_err = false,
		dev_found = false, attr_found = false, ctx_found = false,
		debug_found = false, channel_found = false ;
	const struct iio_attr *attr;
	unsigned int i;
	char *wbuf = NULL;
	struct option *opts;
	int ret = EXIT_FAILURE;

	argw = dup_argv(MY_NAME, argc, argv);

	/*
	 * getopt_long() thinks negative numbers are options, -1 is option '1'
	 * The only time we should see a negative number is the last argument during a write,
	 * so if there is one, we skip that argument during getopt processing
	 * look for "-" followed by a number.
	 */
	if (strnlen(argv[argc - 1], 2) >= 2 && argv[argc - 1][0] == '-' && 
			(argv[argc - 1][1] >= '0' && argv[argc - 1][1] <= '9')) {
		argd--;
	}

	ctx = handle_common_opts(MY_NAME, argd, argw, MY_OPTS,
				 options, options_descriptions, &ret);
	opts = add_common_options(options);
	if (!opts) {
		fprintf(stderr, "Failed to add common options\n");
		return EXIT_FAILURE;
	}
	while ((c = getopt_long(argd, argw, "+" COMMON_OPTIONS MY_OPTS, /* Flawfinder: ignore */
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
			quiet = ATTR_QUIET;
			break;
		case 'v':
			quiet = ATTR_VERBOSE;
			break;
		case 'g':
			if (!optarg || optarg[0] == '-') {
				fprintf(stderr, "Code generation requires an option\n");
				return EXIT_FAILURE;
			}
			gen_code = true;
			gen_file = optarg;
			break;
		case '?':
			printf("Unknown argument '%c'\n", c);
			return EXIT_FAILURE;
		}
	}

	free(opts);

	if (!ctx)
		return ret;

	if (gen_code) {
		if (!gen_test_path(gen_file)) {
			fprintf(stderr, "Can't write to %s to generate file\n", gen_file);
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
			usage(MY_NAME, options, options_descriptions);
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
		if (gen_code && !attr_index) {
			printf("When generating code for Context Attributes, must include specific attribute\n"
					"-C [IIO_context_attribute]\n");
			return EXIT_FAILURE;
		}
	} else if (search_device) {
		/* -d [device] [attr] [value] */
		if (argc >= optind + 1)
			device_index = optind;
		if (argc >= optind + 2)
			attr_index = optind + 1;
		if (argc >= optind + 3)
			wbuf = argw[optind + 2];
		if (argc >= optind + 4) {
			fprintf(stderr, "Too many options for searching for device attributes\n");
			return EXIT_FAILURE;
		}
		if (gen_code && !attr_index) {
			printf("When generating code for device Attributes, must include specific attribute\n"
					"-d [IIO_device] [IIO_device_attr] [value]\n");
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
			wbuf = argw[optind + 3];
		if (argc >= optind + 5) {
			fprintf(stderr, "Too many options for searching for channel attributes\n");
			return EXIT_FAILURE;
		}
		if (gen_code && !attr_index) {
			printf("When generating code for Channel Attributes, must include specific attribute\n"
					"-c [IIO_device] [IIO_device_channel] [IIO_channel_attr] [value]\n");
			return EXIT_FAILURE;
		}
	} else if (search_buffer) {
		/* -B [device] [attribute] [value] */
		if (argc >= optind + 1)
			device_index = optind;
		if (argc >= optind + 2)
			attr_index = optind + 1;
		if (argc >= optind + 3)
			wbuf = argw[optind + 2];
		if (argc >= optind + 4) {
			fprintf(stderr, "Too many options for searching for buffer attributes\n");
			return EXIT_FAILURE;
		}
		if (gen_code && !attr_index) {
			printf("When generating code for Buffer Attributes, must include specific attribute\n"
					"-B [IIO_device] [IIO_buffer_attribute] [value]\n");
			return EXIT_FAILURE;
		}

	} else if (search_debug) {
		/* -D [device] [attribute] [value] */
		if (argc >= optind + 1)
			device_index = optind;
		if (argc >= optind + 2)
			attr_index = optind + 1;
		if (argc >= optind + 3)
			wbuf = argw[optind + 2];
		if (argc >= optind + 4) {
			fprintf(stderr, "Too many options for searching for device attributes\n");
			return EXIT_FAILURE;
		}
		if (gen_code && !attr_index) {
			printf("When generating code for Debug Attributes, must include specific attribute\n"
					"-D [IIO_device] [IIO_debug_attribute] [value]\n");
			return EXIT_FAILURE;
		}
	} else {
		fprintf(stderr, "error in application\n");
		return EXIT_FAILURE;
	}

	if (device_index && !argw[device_index])
		return EXIT_FAILURE;
	if (channel_index && !argw[channel_index])
		return EXIT_FAILURE;
	if (attr_index && !argw[attr_index])
		return EXIT_FAILURE;
	/* check for wildcards */
	if (((device_index && (!strcmp(".", argw[device_index]) ||
				strchr(argw[device_index], '*'))) ||
			(channel_index && (!strcmp(".", argw[channel_index]) ||
				strchr(argw[channel_index], '*'))) ||
			(attr_index && (!strcmp(".", argw[attr_index])  ||
				strchr(argw[attr_index], '*'))))) {
		if (gen_code || wbuf) {
			printf("can't %s with wildcard match\n",
					gen_code ? "generate code" : "write value");
			return EXIT_FAILURE;
		}
		/* Force verbose mode */
		quiet = ATTR_VERBOSE;
	}

	if (gen_code) {
		gen_start(gen_file);
		attr = iio_context_find_attr(ctx, "uri");
		gen_context(iio_attr_get_static_value(attr));
	}

	if (search_context) {
		unsigned int nb_ctx_attrs = iio_context_get_attrs_count(ctx);
		if (!attr_index && nb_ctx_attrs > 0)
			printf("IIO context with %u attributes:\n", nb_ctx_attrs);
		if (!attr_index && !nb_ctx_attrs) {
			printf("%s: Found context, but it has %u context attributes\n",
					MY_NAME, nb_ctx_attrs);
			found_err = true;
		}

		ctx_found = true;
		for (i = 0; i < nb_ctx_attrs; i++) {
			const char *key, *value;

			attr = iio_context_get_attr(ctx, i);
			key = iio_attr_get_name(attr);
			value = iio_attr_get_static_value(attr);

			if (!attr_index || str_match(key, argw[attr_index], ignore_case)) {
				found_err = false;
				attr_found = true;
				printf("%s: %s\n", key, value);
				gen_context_attr(key);
			}
		}
	}

	if (search_device || search_channel || search_buffer || search_debug) {
		unsigned int nb_devices = iio_context_get_devices_count(ctx);

		if (!device_index)
			printf("IIO context has %u devices:\n", nb_devices);

		for (i = 0; i < nb_devices; i++) {
			const struct iio_device *dev = iio_context_get_device(ctx, i);
			const char *dev_id = iio_device_get_id(dev);
			const char *label = iio_device_get_label(dev);
			const char *name = iio_device_get_name(dev);
			const char *label_or_name = label ? label : name;
			const char *label_or_name_or_id = label_or_name ? label_or_name : dev_id;
			const char *ch_name;
			struct iio_buffer *buffer;
			struct iio_channels_mask *mask;
			unsigned int nb_attrs, nb_channels, j;

			if (device_index && !str_match(dev_id, argw[device_index], ignore_case)
					&& !str_match(label, argw[device_index], ignore_case)
					&& !str_match(name, argw[device_index], ignore_case)) {
				continue;
			}
			dev_found = true;

			if ((search_device && !device_index) || (search_channel && !device_index) ||
					(search_buffer && !device_index) || (search_debug && !device_index)) {
				printf("\t%s", dev_id);
				if (label_or_name)
					printf(", %s", label_or_name);
				printf(": ");
			}

			nb_channels = iio_device_get_channels_count(dev);

			if (search_channel && !device_index) {
				if (scan_only || input_only || output_only) {
					unsigned int scan = 0, in = 0, out = 0;
					for (j = 0; j < nb_channels; j++) {
						struct iio_channel *ch;
						ch = iio_device_get_channel(dev, j);
						if (iio_channel_is_output(ch))
							out++;
						else
							in++;
						if (iio_channel_is_scan_element(ch))
							scan++;
					}
					printf("found ");
					if (scan_only)
						printf("%u scan", scan);
					if (output_only) {
						if (scan_only)
							printf(", ");
						printf("%u output", out);
					}
					if (input_only) {
						if (scan_only || output_only)
							printf(", ");
						printf("%u input", in);
					}
					printf(" channels\n");
				} else {
					printf("found %u channels\n", nb_channels);
				}
			}

			if (search_channel && device_index && !channel_index && !nb_channels) {
				printf("%s: Found %s device, but it has %u channels\n",
						MY_NAME, argw[device_index], nb_channels);
				found_err = true;
			}

			mask = nb_channels ? iio_create_channels_mask(nb_channels) : NULL;

			for (j = 0; j < nb_channels; j++) {
				struct iio_channel *ch;
				const char *type_name;
				unsigned int k;

				ch = iio_device_get_channel(dev, j);

				if (mask)
					iio_channel_enable(ch, mask);

				if (!search_channel || !device_index)
					continue;

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

				ch_name = iio_channel_get_name(ch);
				if (channel_index &&
						!str_match(iio_channel_get_id(ch),
						argw[channel_index], ignore_case) &&
						(!ch_name || !str_match(ch_name,argw[channel_index], ignore_case)))
					continue;

				channel_found = true;
				if ((!scan_only && !channel_index) ||
				    ( scan_only && iio_channel_is_scan_element(ch))) {
					printf("%s ", iio_device_is_trigger(dev) ? "trig" : "dev");
					printf("'%s', ", label_or_name_or_id);

					printf("channel '%s'",
						iio_channel_get_id(ch));

					if (ch_name)
						printf(", id '%s'", ch_name);

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
				/* search_channel & device_index are checked in L630 */
				if(channel_index && !attr_index && !nb_attrs) {
					printf("%s: Found %s device, but it has %u channel attributes\n",
							MY_NAME, argw[device_index], nb_attrs);
					found_err = true;
				}

				if (!nb_attrs || !channel_index)
					continue;

				for (k = 0; k < nb_attrs; k++) {
					attr = iio_channel_get_attr(ch, k);

					if (attr_index &&
						!str_match(iio_attr_get_name(attr),
							   argw[attr_index],
							   ignore_case))
						continue;
					gen_dev(dev);
					found_err = false;
					attr_found = true;
					gen_ch(ch);
					ret = dump_channel_attributes(dev, ch, attr, wbuf,
								attr_index ? quiet : ATTR_VERBOSE);
					if (wbuf && ret < 0)
						write_err = true;
					else if (ret < 0 && attr_index)
						read_err = true;
				}
			}

			nb_attrs = iio_device_get_attrs_count(dev);
			if (search_device && !device_index)
				printf("found %u device attributes\n", nb_attrs);
			if (search_device && device_index && !attr_index && !nb_attrs) {
				printf("%s: Found %s device, but it has %u device attributes\n",
						MY_NAME, label_or_name_or_id, nb_attrs);
				if (!attr_found)
					found_err = true;
			}

			if (search_device && device_index && nb_attrs) {
				for (j = 0; j < nb_attrs; j++) {
					attr = iio_device_get_attr(dev, j);

					if (attr_index &&
					    !str_match(iio_attr_get_name(attr),
						       argw[attr_index], ignore_case))
						continue;

					gen_dev(dev);
					found_err = false;
					attr_found = true;
					ret = dump_device_attributes(dev, attr, "device", "dev", wbuf,
								     attr_index ? quiet : ATTR_VERBOSE);
					if (wbuf && ret < 0)
						write_err = true;
					else if (ret < 0 && attr_index)
						read_err = true;
				}
			}

			if (mask)
				buffer = iio_device_create_buffer(dev, 0, mask);
			if (mask && !iio_err(buffer)) {
				nb_attrs = iio_buffer_get_attrs_count(buffer);

				if (search_buffer && !device_index)
					printf("found %u buffer attributes\n", nb_attrs);

				if (search_buffer && device_index && !attr_index && !nb_attrs) {
					printf("%s: Found %s device, but it has %u buffer attributes\n",
					       MY_NAME, label_or_name_or_id, nb_attrs);
					if (!attr_found)
						found_err = true;
				}

				if (search_buffer && device_index && nb_attrs) {
					for (j = 0; j < nb_attrs; j++) {
						attr = iio_buffer_get_attr(buffer, j);

						if ((attr_index && str_match(iio_attr_get_name(attr),
									     argw[attr_index],
									     ignore_case))
						    || !attr_index) {
							found_err = false;
							attr_found = true;
							ret = dump_device_attributes(dev, attr, "buffer",
										     "buf", wbuf,
										     attr_index ? quiet : ATTR_VERBOSE);
							if (wbuf && ret < 0)
								write_err = true;
							else if (ret < 0 && attr_index)
								read_err = true;
						}
					}

				}

				iio_buffer_destroy(buffer);
			}

			if (mask)
				iio_channels_mask_destroy(mask);

			nb_attrs = iio_device_get_debug_attrs_count(dev);

			if (search_debug && !device_index)
				printf("found %u debug attributes\n", nb_attrs);

			if (search_debug && device_index && nb_attrs) {
				for (j = 0; j < nb_attrs; j++) {
					attr = iio_device_get_debug_attr(dev, j);

					if ((attr_index
					     && str_match(iio_attr_get_name(attr),
							  argw[attr_index],
							  ignore_case)) || !attr_index) {
						gen_dev(dev);
						found_err = false;
						attr_found = true;
						debug_found = true;
						ret = dump_device_attributes(dev, attr, "device_debug",
									     "dev", wbuf,
									     attr_index ? quiet : ATTR_VERBOSE);
						if (wbuf && ret < 0)
							write_err = true;
						else if (ret < 0 && attr_index)
							read_err = true;
					}
				}

			}
		}

	}

	iio_context_destroy(ctx);

	if (gen_code)
		gen_context_destroy();

	if (!dev_found && device_index) {
		IIO_ERR("Could not find device (%s)\n", argw[device_index]);
	} else if (!ctx_found && search_context) {
		IIO_ERR("Could not find Context Attributes\n");
	} else if (!channel_found && channel_index) {
		if (input_only)
			IIO_ERR("Could not find Input channel (%s)\n", argw[channel_index]);
		if (output_only)
			IIO_ERR("Could not find Output channel (%s)\n", argw[channel_index]);
		if (scan_only)
			IIO_ERR("Could not find Scan channel (%s)\n", argw[channel_index]);
		if (!input_only && !output_only && !scan_only)
			IIO_ERR("Could not find channel (%s)\n", argw[channel_index]);
	} else if (!attr_found && attr_index)
		IIO_ERR("Could not find attribute (%s)\n", argw[attr_index]);
	else if (!debug_found && search_debug && device_index) {
		IIO_ERR("Device (%s) had 0 debug attributes\n", argw[device_index]);
	}

	free_argw(argc, argw);

	if ((!dev_found && device_index) || (!ctx_found && search_context) ||
			(!channel_found && channel_index) || (!attr_found && attr_index) ||
			(!debug_found && search_debug && device_index))
		return EXIT_FAILURE;

	if (write_err || read_err || found_err)
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}
