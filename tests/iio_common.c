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
#include <inttypes.h>
#include <getopt.h>

#include "iio_common.h"
#include "gen_code.h"

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
		fprintf(stderr, "%s fatal error: allocating %zu bytes failed\n",
				name, n);
		exit(EXIT_FAILURE);
	}

	return p;
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
		iio_strerror(-ret, err_str, BUF_SIZE);
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
	unsigned long int val;
	char buf[20];

	if (!argv) {
		val = 0;
	} else {
		/* sanitized buffer by taking first 20 (or less) char */
		iio_snprintf(buf, sizeof(buf), "%s", argv);
		val = strtoul(buf, NULL, 10);
	}

	if (val > max) {
		val = max;
		fprintf(stderr, "Clamped %s to max %" PRIu64 "\n", name, max);
	}
	if (val < min) {
		val = min;
		fprintf(stderr, "Clamped %s to min %" PRIu64 "\n", name, min);
	}
	return val;
}


void usage(char *name, const struct option *options,
	const char *options_descriptions[])
{
	unsigned int i;

	printf("Usage:\n");
	printf("\t%s [OPTION]...\t%s\n", name, options_descriptions[0]);
	printf("Options:\n");
	for (i = 0; options[i].name; i++) {
		printf("\t-%c, --%s\n\t\t\t%s\n",
				options[i].val, options[i].name,
			options_descriptions[i + 1]);
	}
}

