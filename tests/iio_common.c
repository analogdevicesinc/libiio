/*
 * iio_common - Common functions used in the IIO utilities
 *
 * Copyright (C) 2014-2020 Analog Devices, Inc.
 * Author: Paul Cercueil
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

#include <iio.h>
#include <stdio.h>
#include <errno.h>
#include <inttypes.h>
#include <getopt.h>
#include <string.h>

#include "iio_common.h"
#include "gen_code.h"
#include "iio-config.h"

#ifdef _MSC_BUILD
#define inline __inline
#define iio_snprintf sprintf_s
#else
#define iio_snprintf snprintf
#endif

void * xmalloc(size_t n, const char * name)
{
	void *p = malloc(n);

	if (!p && n != 0) {
		if (name) {
			fprintf(stderr, "%s fatal error: allocating %zu bytes failed\n",
				name, n);
		} else {
			fprintf(stderr, "Fatal error: allocating %zu bytes failed\n", n);
		}
		exit(EXIT_FAILURE);
	}

	return p;
}

char *cmn_strndup(const char *str, size_t n)
{
#ifdef HAS_STRNDUP
	return strndup(str, n);
#else
	size_t len = strnlen(str, n + 1);
	char *buf = malloc(len + 1);
	if (buf) {
		/* len = size of buf, so memcpy is OK */
		memcpy(buf, str, len); /* Flawfinder: ignore */
		buf[len] = 0;
	}
	return buf;
#endif
}



struct iio_context * autodetect_context(bool rtn, bool gen_code, const char * name)
{
	struct iio_scan_context *scan_ctx;
	struct iio_context_info **info;
	struct iio_context *ctx = NULL;
	unsigned int i;
	ssize_t ret;
	FILE *out;

	scan_ctx = iio_create_scan_context(NULL, 0);
	if (!scan_ctx) {
		fprintf(stderr, "Unable to create scan context\n");
		return NULL;
	}

	ret = iio_scan_context_get_info_list(scan_ctx, &info);
	if (ret < 0) {
		char *err_str = xmalloc(BUF_SIZE, name);
		iio_strerror(-(int)ret, err_str, BUF_SIZE);
		fprintf(stderr, "Scanning for IIO contexts failed: %s\n", err_str);
		free (err_str);
		goto err_free_ctx;
	}

	if (ret == 0) {
		printf("No IIO context found.\n");
		goto err_free_info_list;
	}
	if (rtn && ret == 1) {
		printf("Using auto-detected IIO context at URI \"%s\"\n",
		iio_context_info_get_uri(info[0]));
		ctx = iio_create_context_from_uri(iio_context_info_get_uri(info[0]));
		if (gen_code)
			gen_context(iio_context_info_get_uri(info[0]));
	} else {
		if (rtn) {
			out = stderr;
			fprintf(out, "Multiple contexts found. Please select one using --uri:\n");
		} else {
			out = stdout;
			fprintf(out, "Available contexts:\n");
		}
		for (i = 0; i < (size_t) ret; i++) {
			fprintf(out, "\t%u: %s [%s]\n",
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

unsigned long int sanitize_clamp(const char *name, const char *argv,
	uint64_t min, uint64_t max)
{
	uint64_t val;
	char buf[20], *end;

	if (!argv) {
		val = 0;
	} else {
		/* sanitized buffer by taking first 20 (or less) char */
		iio_snprintf(buf, sizeof(buf), "%s", argv);
		errno = 0;
		val = strtoul(buf, &end, 10);
		if (buf == end || errno == ERANGE)
			val = 0;
	}

	if (val > max) {
		val = max;
		fprintf(stderr, "Clamped %s to max %" PRIu64 "\n", name, max);
	}
	if (val < min) {
		val = min;
		fprintf(stderr, "Clamped %s to min %" PRIu64 "\n", name, min);
	}
	return (unsigned long int) val;
}

char ** dup_argv(char * name, int argc, char * argv[])
{
	int i = 1;
	char** new_argv = xmalloc((argc + 1) * sizeof(char *), name);

	for(int i = 0; i < argc; i++) {
		new_argv[i] = cmn_strndup(argv[i], NAME_MAX);
		if (!new_argv[i])
			goto err_dup;
	}

	return new_argv;

err_dup:
	i--;
	for (; i >= 0; i--)
		free(new_argv[i]);

	free(new_argv);

	fprintf(stderr, "out of memory\n");
	exit(0);
}

void free_argw(int argc, char * argw[])
{
	int i;

	for(i = 0; i < argc; i++) {
		free(argw[i]);
	}
	free(argw);
}

static const struct option common_options[] = {
	{"help", no_argument, 0, 'h'},
	{"xml", required_argument, 0, 'x'},
	{"uri", required_argument, 0, 'u'},
	{"scan", no_argument, 0, 'S'},
	{"auto", no_argument, 0, 'a'},
	{0, 0, 0, 0},
};

static const char *common_options_descriptions[] = {
	"Show this help and quit.",
	"Use the XML backend with the provided XML file.",
	"Use the context at the provided URI.",
	"Scan for available backends.",
	"Scan for available contexts and if only one is available use it.",
};


struct iio_context * handle_common_opts(char * name, int argc, char * const argv[],
		const struct option *options, const char *options_descriptions[])
{
	struct iio_context *ctx = NULL;
	int c, option_index = 0;
	enum backend backend = IIO_LOCAL;
	const char *arg = NULL;
	bool do_scan = false, detect_context = false;

	/* Setting opterr to zero disables error messages from getopt_long */
	opterr = 0;
	/* start over at first index */
	optind = 1;
	while ((c = getopt_long(argc, argv, COMMON_OPTIONS,	/* Flawfinder: ignore */
			common_options, &option_index)) != -1) {
		switch (c) {
		case 'h':
			usage(name, options, options_descriptions);
			break;
		case 'n':
			if (backend != IIO_LOCAL) {
				fprintf(stderr, "-a, -x, -n and -u are mutually exclusive\n");
				return NULL;
			}
			backend = IIO_NETWORK;
			arg = optarg;
			break;
		case 'x':
			if (backend != IIO_LOCAL) {
				fprintf(stderr, "-a, -x, -n and -u are mutually exclusive\n");
				return NULL;
			}
			backend = IIO_XML;
			arg = optarg;
			break;
		case 'u':
			if (backend != IIO_LOCAL) {
				fprintf(stderr, "-a, -x, -n and -u are mutually exclusive\n");
				return NULL;
			}
			backend = IIO_AUTO;
			arg = optarg;
			break;
		case 'a':
			if (backend != IIO_LOCAL) {
				fprintf(stderr, "-a, -x, -n and -u are mutually exclusive\n");
				return NULL;
			}
			detect_context = true;
			break;
		case 'S':
			do_scan = true;
			break;
		case '?':
			break;
		}
	}
	optind = 1;
	opterr = 1;

	if (do_scan) {
		autodetect_context(false, false, name);
		exit(0);
	} else if (detect_context)
		ctx = autodetect_context(true, false, name);
	else if (!arg && backend != IIO_LOCAL)
		fprintf(stderr, "argument parsing error\n");
	else if (backend == IIO_XML)
		ctx = iio_create_xml_context(arg);
	else if (backend == IIO_NETWORK)
		ctx = iio_create_network_context(arg);
	else if (backend == IIO_AUTO)
		ctx = iio_create_context_from_uri(arg);
	else
		ctx = iio_create_default_context();

	if (!ctx && !do_scan && !detect_context) {
		char buf[1024];
		iio_strerror(errno, buf, sizeof(buf));
		if (arg)
			fprintf(stderr, "Unable to create IIO context %s: %s\n", arg, buf);
		else
			fprintf(stderr, "Unable to create Local IIO context : %s\n", buf);
	}

	return ctx;
}

void usage(char *name, const struct option *options,
	const char *options_descriptions[])
{
	unsigned int i;

	printf("Usage:\n");
	printf("\t%s [OPTION]...\t%s\n", name, options_descriptions[0]);
	printf("Options:\n");
	for (i = 0; common_options[i].name; i++) {
		printf("\t-%c, --%s\n\t\t\t%s\n",
				common_options[i].val, common_options[i].name,
				common_options_descriptions[i]);
	}
	for (i = 0; options[i].name; i++) {
		printf("\t-%c, --%s\n\t\t\t%s\n",
				options[i].val, options[i].name,
			options_descriptions[i + 1]);
	}
	printf("\nThis is free software; see the source for copying conditions.  There is NO\n"
			"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n\n");

	exit(0);
}

