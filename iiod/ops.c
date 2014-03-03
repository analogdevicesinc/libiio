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

#include "ops.h"
#include "parser.h"

#include <errno.h>
#include <string.h>

int yyparse(yyscan_t scanner);

static ssize_t write_all(const void *src, size_t len, FILE *out)
{
	const void *ptr = src;
	while (len) {
		ssize_t ret = fwrite(ptr, 1, len, out);
		if (ret < 0)
			return ret;
		ptr += ret;
		len -= ret;
	}
	return ptr - src;
}

static struct iio_device * get_device(struct iio_context *ctx, const char *id)
{
	unsigned int i, nb_devices = iio_context_get_devices_count(ctx);

	for (i = 0; i < nb_devices; i++) {
		struct iio_device *dev = iio_context_get_device(ctx, i);
		if (!strcmp(id, iio_device_get_id(dev))
				|| !strcmp(id, iio_device_get_name(dev)))
			return dev;
	}

	return NULL;
}

ssize_t read_dev_attr(struct parser_pdata *pdata,
		const char *id, const char *attr)
{
	FILE *out = pdata->out;
	struct iio_device *dev = get_device(pdata->ctx, id);
	char buf[1024], cr = '\n';
	ssize_t ret;

	if (!dev) {
		if (pdata->verbose) {
			strerror_r(ENODEV, buf, 1024);
			fprintf(out, "ERROR: %s\n", buf);
		} else {
			fprintf(out, "%i\n", -ENODEV);
		}
		return -ENODEV;
	}

	ret = iio_device_attr_read(dev, attr, buf, 1024);
	if (pdata->verbose && ret < 0) {
		strerror_r(-ret, buf, 1024);
		fprintf(out, "ERROR: %s\n", buf);
	} else {
		fprintf(out, "%li\n", ret);
	}
	if (ret < 0)
		return ret;

	ret = write_all(buf, ret, out);
	write_all(&cr, 1, out);
	return ret;
}

ssize_t write_dev_attr(struct parser_pdata *pdata,
		const char *id, const char *attr, const char *value)
{
	FILE *out = pdata->out;
	struct iio_device *dev = get_device(pdata->ctx, id);
	if (!dev) {
		if (pdata->verbose) {
			char buf[1024];
			strerror_r(ENODEV, buf, 1024);
			fprintf(out, "ERROR: %s\n", buf);
		} else {
			fprintf(out, "%i\n", -ENODEV);
		}
		return -ENODEV;
	} else {
		ssize_t ret = iio_device_attr_write(dev, attr, value);
		if (pdata->verbose && ret < 0) {
			char buf[1024];
			strerror_r(-ret, buf, 1024);
			fprintf(out, "ERROR: %s\n", buf);
		} else {
			fprintf(out, "%li\n", ret);
		}
		return ret;
	}
}

void interpreter(struct iio_context *ctx, FILE *in, FILE *out, bool verbose)
{
	yyscan_t scanner;
	struct parser_pdata pdata;

	pdata.ctx = ctx;
	pdata.stop = false;
	pdata.in = in;
	pdata.out = out;
	pdata.verbose = verbose;

	yylex_init_extra(&pdata, &scanner);
	yyset_out(out, scanner);
	yyset_in(in, scanner);

	do {
		if (verbose) {
			fprintf(out, "iio-daemon > ");
			fflush(out);
		}
		yyparse(scanner);
		if (pdata.stop)
			break;
	} while (!feof(in));

	yylex_destroy(scanner);
}
