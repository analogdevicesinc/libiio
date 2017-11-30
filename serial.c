/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014-2016 Analog Devices, Inc.
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

#include "debug.h"
#include "iio-private.h"
#include "iio-lock.h"
#include "iiod-client.h"

#include <errno.h>
#include <libserialport.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_TIMEOUT_MS 1000

struct iio_context_pdata {
	struct sp_port *port;
	struct iio_mutex *lock;
	struct iiod_client *iiod_client;

	unsigned int timeout_ms;
};

struct iio_device_pdata {
	bool opened;
};

static inline int libserialport_to_errno(enum sp_return ret)
{
	switch (ret) {
	case SP_ERR_ARG:
		return -EINVAL;
	case SP_ERR_FAIL:
		return -sp_last_error_code();
	case SP_ERR_MEM:
		return -ENOMEM;
	case SP_ERR_SUPP:
		return -ENOSYS;
	default:
		return (int) ret;
	}
}

static int serial_get_version(const struct iio_context *ctx,
		unsigned int *major, unsigned int *minor, char git_tag[8])
{
	struct iio_context_pdata *pdata = ctx->pdata;

	return iiod_client_get_version(pdata->iiod_client, NULL,
			major, minor, git_tag);
}

static int serial_open(const struct iio_device *dev,
		size_t samples_count, bool cyclic)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *ctx_pdata = ctx->pdata;
	struct iio_device_pdata *pdata = dev->pdata;
	int ret = -EBUSY;

	iio_mutex_lock(ctx_pdata->lock);
	if (pdata->opened)
		goto out_unlock;

	ret = iiod_client_open_unlocked(ctx_pdata->iiod_client, NULL,
			dev, samples_count, cyclic);

	pdata->opened = !ret;

out_unlock:
	iio_mutex_unlock(ctx_pdata->lock);
	return ret;
}

static int serial_close(const struct iio_device *dev)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *ctx_pdata = ctx->pdata;
	struct iio_device_pdata *pdata = dev->pdata;
	int ret = -EBADF;

	iio_mutex_lock(ctx_pdata->lock);
	if (!pdata->opened)
		goto out_unlock;

	ret = iiod_client_close_unlocked(ctx_pdata->iiod_client, NULL, dev);
	pdata->opened = false;

out_unlock:
	iio_mutex_unlock(ctx_pdata->lock);
	return ret;
}

static ssize_t serial_read(const struct iio_device *dev, void *dst, size_t len,
		uint32_t *mask, size_t words)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = ctx->pdata;
	ssize_t ret;

	iio_mutex_lock(pdata->lock);
	ret = iiod_client_read_unlocked(pdata->iiod_client, NULL,
			dev, dst, len, mask, words);
	iio_mutex_unlock(pdata->lock);

	return ret;
}

static ssize_t serial_write(const struct iio_device *dev,
		const void *src, size_t len)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = ctx->pdata;
	ssize_t ret;

	iio_mutex_lock(pdata->lock);
	ret = iiod_client_write_unlocked(pdata->iiod_client, NULL, dev, src, len);
	iio_mutex_unlock(pdata->lock);

	return ret;
}

static ssize_t serial_read_dev_attr(const struct iio_device *dev,
		const char *attr, char *dst, size_t len, enum iio_attr_type type)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = ctx->pdata;

	return iiod_client_read_attr(pdata->iiod_client, NULL,
			dev, NULL, attr, dst, len, type);
}

static ssize_t serial_write_dev_attr(const struct iio_device *dev,
		const char *attr, const char *src, size_t len, enum iio_attr_type type)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = ctx->pdata;

	return iiod_client_write_attr(pdata->iiod_client, NULL,
			dev, NULL, attr, src, len, type);
}

static ssize_t serial_read_chn_attr(const struct iio_channel *chn,
		const char *attr, char *dst, size_t len)
{
	const struct iio_device *dev = iio_channel_get_device(chn);
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = ctx->pdata;

	return iiod_client_read_attr(pdata->iiod_client, NULL,
			chn->dev, chn, attr, dst, len, false);
}

static ssize_t serial_write_chn_attr(const struct iio_channel *chn,
		const char *attr, const char *src, size_t len)
{
	const struct iio_device *dev = iio_channel_get_device(chn);
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = ctx->pdata;

	return iiod_client_write_attr(pdata->iiod_client, NULL,
			dev, chn, attr, src, len, false);
}

static int serial_set_kernel_buffers_count(const struct iio_device *dev,
		unsigned int nb_blocks)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = ctx->pdata;

	return iiod_client_set_kernel_buffers_count(pdata->iiod_client, NULL,
			dev, nb_blocks);
}

static ssize_t serial_write_data(struct iio_context_pdata *pdata,
		void *io_data, const char *data, size_t len)
{
	ssize_t ret = (ssize_t) libserialport_to_errno(sp_blocking_write(
				pdata->port, data, len, pdata->timeout_ms));

	DEBUG("Write returned %li: %s\n", (long) ret, data);
	return ret;
}

static ssize_t serial_read_data(struct iio_context_pdata *pdata,
		void *io_data, char *buf, size_t len)
{
	ssize_t ret = (ssize_t) libserialport_to_errno(sp_blocking_read_next(
				pdata->port, buf, len, pdata->timeout_ms));

	DEBUG("Read returned %li: %.*s\n", (long) ret, (int) ret, buf);
	return ret;
}

static ssize_t serial_read_line(struct iio_context_pdata *pdata,
		void *io_data, char *buf, size_t len)
{
	size_t i;
	bool found = false;
	int ret;

	DEBUG("Readline size 0x%lx\n", (unsigned long) len);

	for (i = 0; i < len - 1; i++) {
		ret = libserialport_to_errno(sp_blocking_read_next(
					pdata->port, &buf[i], 1,
					pdata->timeout_ms));
		if (ret < 0) {
			ERROR("sp_blocking_read_next returned %i\n", ret);
			return (ssize_t) ret;
		}

		DEBUG("Character: %c\n", buf[i]);

		if (buf[i] != '\n')
			found = true;
		else if (found)
			break;
	}

	/* No \n found? Just garbage data */
	if (!found || i == len - 1)
		return -EIO;

	return (ssize_t) i + 1;
}

static void serial_shutdown(struct iio_context *ctx)
{
	struct iio_context_pdata *ctx_pdata = ctx->pdata;
	unsigned int i;

	iiod_client_destroy(ctx_pdata->iiod_client);
	iio_mutex_destroy(ctx_pdata->lock);
	sp_close(ctx_pdata->port);
	sp_free_port(ctx_pdata->port);

	for (i = 0; i < iio_context_get_devices_count(ctx); i++) {
		const struct iio_device *dev = iio_context_get_device(ctx, i);
		struct iio_device_pdata *pdata = dev->pdata;

		free(pdata);
	}

	free(ctx_pdata);
}

static int serial_set_timeout(struct iio_context *ctx, unsigned int timeout)
{
	ctx->pdata->timeout_ms = timeout;
	return 0;
}

static const struct iio_backend_ops serial_ops = {
	.get_version = serial_get_version,
	.open = serial_open,
	.close = serial_close,
	.read = serial_read,
	.write = serial_write,
	.read_device_attr = serial_read_dev_attr,
	.write_device_attr = serial_write_dev_attr,
	.read_channel_attr = serial_read_chn_attr,
	.write_channel_attr = serial_write_chn_attr,
	.set_kernel_buffers_count = serial_set_kernel_buffers_count,
	.shutdown = serial_shutdown,
	.set_timeout = serial_set_timeout,
};

static const struct iiod_client_ops serial_iiod_client_ops = {
	.write = serial_write_data,
	.read = serial_read_data,
	.read_line = serial_read_line,
};

static int apply_settings(struct sp_port *port, unsigned int baud_rate,
		unsigned int bits, unsigned int stop_bits,
		enum sp_parity parity, enum sp_flowcontrol flow)
{
	int ret;

	ret = libserialport_to_errno(sp_set_baudrate(port, (int) baud_rate));
	if (ret)
		return ret;

	ret = libserialport_to_errno(sp_set_bits(port, (int) bits));
	if (ret)
		return ret;

	ret = libserialport_to_errno(sp_set_stopbits(port, (int) stop_bits));
	if (ret)
		return ret;

	ret = libserialport_to_errno(sp_set_parity(port, parity));
	if (ret)
		return ret;

	return libserialport_to_errno(sp_set_flowcontrol(port, flow));
}

static struct iio_context * serial_create_context(const char *port_name,
		unsigned int baud_rate, unsigned int bits,
		enum sp_parity parity, enum sp_flowcontrol flow)
{
	struct sp_port *port;
	struct iio_context_pdata *pdata;
	struct iio_context *ctx;
	char *name, *desc, *description;
	size_t desc_len;
	unsigned int i;
	int ret;

	ret = libserialport_to_errno(sp_get_port_by_name(port_name, &port));
	if (ret) {
		errno = -ret;
		return NULL;
	}

	ret = libserialport_to_errno(sp_open(port, SP_MODE_READ_WRITE));
	if (ret) {
		errno = -ret;
		goto err_free_port;
	}

	ret = apply_settings(port, baud_rate, bits, 1, parity, flow);
	if (ret) {
		errno = -ret;
		goto err_close_port;
	}

	/* Empty the buffers */
	sp_flush(port, SP_BUF_BOTH);

	name = sp_get_port_name(port);
	desc = sp_get_port_description(port);

	desc_len = sizeof(": \0") + strlen(name) + strlen(desc);
	description = malloc(desc_len);
	if (!description) {
		errno = ENOMEM;
		goto err_close_port;
	}

	iio_snprintf(description, desc_len, "%s: %s", name, desc);

	pdata = zalloc(sizeof(*pdata));
	if (!pdata) {
		errno = ENOMEM;
		goto err_free_description;
	}

	pdata->port = port;
	pdata->timeout_ms = DEFAULT_TIMEOUT_MS;

	pdata->lock = iio_mutex_create();
	if (!pdata->lock) {
		errno = ENOMEM;
		goto err_free_pdata;
	}

	pdata->iiod_client = iiod_client_new(pdata, pdata->lock,
			&serial_iiod_client_ops);
	if (!pdata->iiod_client)
		goto err_destroy_mutex;

	ctx = iiod_client_create_context(pdata->iiod_client, NULL);
	if (!ctx)
		goto err_destroy_iiod_client;

	ctx->name = "serial";
	ctx->ops = &serial_ops;
	ctx->pdata = pdata;
	ctx->description = description;

	for (i = 0; i < iio_context_get_devices_count(ctx); i++) {
		struct iio_device *dev = iio_context_get_device(ctx, i);

		dev->pdata = zalloc(sizeof(*dev->pdata));
		if (!dev->pdata) {
			ret = -ENOMEM;
			goto err_context_destroy;
		}
	}

	return ctx;

err_context_destroy:
	iio_context_destroy(ctx);
	errno = -ret;
	return NULL;

err_destroy_iiod_client:
	iiod_client_destroy(pdata->iiod_client);
err_destroy_mutex:
	iio_mutex_destroy(pdata->lock);
err_free_pdata:
	free(pdata);
err_free_description:
	free(description);
err_close_port:
	sp_close(port);
err_free_port:
	sp_free_port(port);
	return NULL;
}

static int serial_parse_params(const char *params,
		unsigned int *baud_rate, unsigned int *bits,
		enum sp_parity *parity, enum sp_flowcontrol *flow)
{
	char *end;

	*baud_rate = strtoul(params, &end, 10);
	if (params == end)
		return -EINVAL;

	switch (*end) {
	case '\0':
		/* Default settings */
		*bits = 8;
		*parity = SP_PARITY_NONE;
		*flow = SP_FLOWCONTROL_NONE;
		return 0;
	case 'n':
		*parity = SP_PARITY_NONE;
		break;
	case 'o':
		*parity = SP_PARITY_ODD;
		break;
	case 'e':
		*parity = SP_PARITY_EVEN;
		break;
	case 'm':
		*parity = SP_PARITY_MARK;
		break;
	case 's':
		*parity = SP_PARITY_SPACE;
		break;
	default:
		return -EINVAL;
	}

	params = (const char *)((uintptr_t) end + 1);

	if (!*params) {
		*bits = 8;
		*flow = SP_FLOWCONTROL_NONE;
		return 0;
	}

	*bits = strtoul(params, &end, 10);
	if (params == end)
		return -EINVAL;

	switch (*end) {
	case '\0':
		*flow = SP_FLOWCONTROL_NONE;
		return 0;
	case 'x':
		*flow = SP_FLOWCONTROL_XONXOFF;
		break;
	case 'r':
		*flow = SP_FLOWCONTROL_RTSCTS;
		break;
	case 'd':
		*flow = SP_FLOWCONTROL_DTRDSR;
		break;
	default:
		return -EINVAL;
	}

	/* We should have a '\0' after the flow character */
	if (end[1])
		return -EINVAL;
	else
		return 0;
}

struct iio_context * serial_create_context_from_uri(const char *uri)
{
	struct iio_context *ctx = NULL;
	char *comma, *uri_dup;
	unsigned int baud_rate, bits;
	enum sp_parity parity;
	enum sp_flowcontrol flow;
	int ret;

	if (strncmp(uri, "serial:", sizeof("serial:") - 1) != 0)
		goto err_bad_uri;

	uri_dup = iio_strdup((const char *)
			((uintptr_t) uri + sizeof("serial:") - 1));
	if (!uri_dup) {
		errno = ENOMEM;
		return NULL;
	}

	comma = strchr(uri_dup, ',');
	if (!comma)
		goto err_free_dup;

	*comma = '\0';

	ret = serial_parse_params((char *)((uintptr_t) comma + 1),
			&baud_rate, &bits, &parity, &flow);
	if (ret)
		goto err_free_dup;

	ctx = serial_create_context(uri_dup, baud_rate, bits, parity, flow);

	free(uri_dup);
	return ctx;

err_free_dup:
	free(uri_dup);
err_bad_uri:
	ERROR("Bad URI: \'%s\'\n", uri);
	errno = EINVAL;
	return NULL;
}
