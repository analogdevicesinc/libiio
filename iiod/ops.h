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

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

struct parser_pdata {
	struct iio_context *ctx;
	bool stop, verbose;
	FILE *in, *out;

	/* Used as temporaries placements by the lexer */
	struct iio_device *dev;
	struct iio_channel *chn;
	bool channel_is_output;
};

extern bool server_demux; /* Defined in iiod.c */

void interpreter(struct iio_context *ctx, FILE *in, FILE *out, bool verbose);

int open_dev(struct parser_pdata *pdata, struct iio_device *dev,
		size_t samples_count, const char *mask, bool cyclic);
int close_dev(struct parser_pdata *pdata, struct iio_device *dev);

ssize_t rw_dev(struct parser_pdata *pdata, struct iio_device *dev,
		unsigned int nb, bool is_write);

ssize_t read_dev_attr(struct parser_pdata *pdata, struct iio_device *dev,
		const char *attr, bool is_debug);
ssize_t write_dev_attr(struct parser_pdata *pdata, struct iio_device *dev,
		const char *attr, size_t len, bool is_debug);

ssize_t read_chn_attr(struct parser_pdata *pdata, struct iio_channel *chn,
		const char *attr);
ssize_t write_chn_attr(struct parser_pdata *pdata, struct iio_channel *chn,
		const char *attr, size_t len);

ssize_t get_trigger(struct parser_pdata *pdata, struct iio_device *dev);
ssize_t set_trigger(struct parser_pdata *pdata,
		struct iio_device *dev, const char *trig);

int set_timeout(struct parser_pdata *pdata, unsigned int timeout);

static __inline__ ssize_t writefd(int fd, const void *buf, size_t len)
{
	ssize_t ret = send(fd, buf, len, MSG_NOSIGNAL);
	if (ret < 0 && errno == ENOTSOCK)
		ret = write(fd, buf, len);
	return ret;
}

static __inline__ void output(struct parser_pdata *pdata, const char *text)
{
	int fd = fileno(pdata->out);
	if (writefd(fd, text, strlen(text)) < 0)
		pdata->stop = true;
}

static __inline__ ssize_t readfd(int fd, void *buf, size_t len)
{
	ssize_t ret = recv(fd, buf, len, MSG_NOSIGNAL);
	if (ret < 0 && errno == ENOTSOCK)
		ret = read(fd, buf, len);
	return ret;
}

#endif /* __OPS_H__ */
