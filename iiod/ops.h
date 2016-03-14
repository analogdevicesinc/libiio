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
#include <sys/queue.h>
#include <unistd.h>

struct parser_pdata {
	struct iio_context *ctx;
	bool stop, verbose;
	int fd_in, fd_out;

	SLIST_HEAD(ParserDataThdHead, ThdEntry) thdlist_head;

	/* Used as temporaries placements by the lexer */
	struct iio_device *dev;
	struct iio_channel *chn;
	bool channel_is_output;
	bool fd_in_is_socket, fd_out_is_socket;
};

extern bool server_demux; /* Defined in iiod.c */

void interpreter(struct iio_context *ctx, int fd_in, int fd_out, bool verbose,
	bool is_socket);

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
int set_buffers_count(struct parser_pdata *pdata,
		struct iio_device *dev, long value);

ssize_t read_line(struct parser_pdata *pdata, char *buf, size_t len);

static __inline__ ssize_t writefd(struct parser_pdata *pdata,
		const void *buf, size_t len)
{
	ssize_t ret;

	if (pdata->fd_out_is_socket)
		ret = send(pdata->fd_out, buf, len, MSG_NOSIGNAL);
	else if (!pdata->fd_out_is_socket)
		ret = write(pdata->fd_out, buf, len);

	if (ret < 0)
		return -errno;
	return ret;
}

static __inline__ void output(struct parser_pdata *pdata, const char *text)
{
	if (writefd(pdata, text, strlen(text)) <= 0)
		pdata->stop = true;
}

static __inline__ ssize_t readfd(struct parser_pdata *pdata,
		void *buf, size_t len)
{
	ssize_t ret;

	if (pdata->fd_in_is_socket)
		ret = recv(pdata->fd_in, buf, len, MSG_NOSIGNAL);
	else if (!pdata->fd_in_is_socket)
		ret = read(pdata->fd_in, buf, len);

	if (ret < 0)
		return -errno;
	return ret;
}

#endif /* __OPS_H__ */
