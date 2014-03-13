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

#ifndef __OPS_H__
#define __OPS_H__

#include "../iio.h"

#include <stdint.h>
#include <stdio.h>

struct parser_pdata {
	struct iio_context *ctx;
	bool stop, verbose, opened;
	FILE *in, *out;
};

void interpreter(struct iio_context *ctx, FILE *in, FILE *out, bool verbose);

int open_dev(struct parser_pdata *pdata, const char *id, const char *mask);
int close_dev(struct parser_pdata *pdata, const char *id);

ssize_t read_dev(struct parser_pdata *pdata, const char *id,
		unsigned int nb, unsigned int samples_size);

ssize_t read_dev_attr(struct parser_pdata *pdata,
		const char *id, const char *attr);
ssize_t write_dev_attr(struct parser_pdata *pdata,
		const char *id, const char *attr, const char *value);

ssize_t read_chn_attr(struct parser_pdata *pdata, const char *id,
		const char *chn, const char *attr);
ssize_t write_chn_attr(struct parser_pdata *pdata, const char *id,
		const char *chn, const char *attr, const char *value);

ssize_t get_trigger(struct parser_pdata *pdata, const char *id);
ssize_t set_trigger(struct parser_pdata *pdata,
		const char *id, const char *trigger);

#endif /* __OPS_H__ */
