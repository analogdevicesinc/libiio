// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014-2016 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "iio-config.h"

#include <iio/iio-backend.h>
#include <iio/iio-debug.h>
#include <iio/iio-lock.h>
#include <iio/iiod-client.h>

#include <errno.h>
#include <libserialport.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

struct iio_context_pdata {
	struct sp_port *port;
	struct iiod_client *iiod_client;

	struct iio_context_params params;
};

struct iio_buffer_pdata {
	struct iiod_client_buffer_pdata *pdata;
};

struct p_options {
	char flag;
	enum sp_parity parity;
};

struct f_options {
	char flag;
	enum sp_flowcontrol flowcontrol;
};

static const struct p_options parity_options[] = {
	{'n', SP_PARITY_NONE},
	{'o', SP_PARITY_ODD},
	{'e', SP_PARITY_EVEN},
	{'m', SP_PARITY_MARK},
	{'s', SP_PARITY_SPACE},
	{'\0', SP_PARITY_INVALID},
};

static const struct f_options flow_options[] = {
	{'n', SP_FLOWCONTROL_NONE},
	{'x', SP_FLOWCONTROL_XONXOFF},
	{'r', SP_FLOWCONTROL_RTSCTS},
	{'d', SP_FLOWCONTROL_DTRDSR},
	{'\0', SP_FLOWCONTROL_NONE},
};

static struct iio_context *
serial_create_context_from_args(const struct iio_context_params *params,
				const char *args);

static char flow_char(enum sp_flowcontrol fc)
{
	unsigned int i;

	for (i = 0; flow_options[i].flag != '\0'; i++) {
		if (fc == flow_options[i].flowcontrol)
			return flow_options[i].flag;
	}
	return '\0';
}

static char parity_char(enum sp_parity pc)
{
	unsigned int i;

	for (i = 0; parity_options[i].flag != '\0'; i++) {
		if (pc == parity_options[i].parity)
			return parity_options[i].flag;
	}
	return '\0';
}

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

static ssize_t
serial_read_attr(const struct iio_attr *attr, char *dst, size_t len)
{
	const struct iio_device *dev = iio_attr_get_device(attr);
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);

	return iiod_client_attr_read(pdata->iiod_client, attr, dst, len);
}

static ssize_t
serial_write_attr(const struct iio_attr *attr, const char *src, size_t len)
{
	const struct iio_device *dev = iio_attr_get_device(attr);
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);

	return iiod_client_attr_write(pdata->iiod_client, attr, src, len);
}

static ssize_t serial_write_data(struct iiod_client_pdata *io_data,
				 const char *data, size_t len,
				 unsigned int timeout_ms)
{
	struct iio_context_pdata *pdata = (struct iio_context_pdata *) io_data;
	enum sp_return sp_ret;
	ssize_t ret;

	sp_ret = sp_blocking_write(pdata->port, data, len, timeout_ms);
	ret = (ssize_t) libserialport_to_errno(sp_ret);

	prm_dbg(&pdata->params, "Write returned %li: %s\n", (long) ret, data);

	if (ret < 0) {
		prm_err(&pdata->params, "sp_blocking_write returned %i\n",
			(int) ret);
		return ret;
	} else if ((size_t) ret < len) {
		prm_err(&pdata->params, "sp_blocking_write has timed out\n");
		return -ETIMEDOUT;
	}

	return ret;
}

static ssize_t serial_read_data(struct iiod_client_pdata *io_data,
				char *buf, size_t len, unsigned int timeout_ms)
{
	struct iio_context_pdata *pdata = (struct iio_context_pdata *) io_data;
	enum sp_return sp_ret;
	ssize_t ret;

	sp_ret = sp_blocking_read_next(pdata->port, buf, len, timeout_ms);
	ret = (ssize_t) libserialport_to_errno(sp_ret);

	prm_dbg(&pdata->params, "Read returned %li: %.*s\n",
		(long) ret, (int) ret, buf);

	if (ret == 0) {
		prm_err(&pdata->params, "sp_blocking_read_next has timed out\n");
		return -ETIMEDOUT;
	}

	return ret;
}

static void serial_shutdown(struct iio_context *ctx)
{
	struct iio_context_pdata *ctx_pdata = iio_context_get_pdata(ctx);

	iiod_client_destroy(ctx_pdata->iiod_client);
	sp_close(ctx_pdata->port);
	sp_free_port(ctx_pdata->port);
}

static const struct iio_device *
serial_get_trigger(const struct iio_device *dev)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);

	return iiod_client_get_trigger(pdata->iiod_client, dev);
}

static int serial_set_trigger(const struct iio_device *dev,
			      const struct iio_device *trigger)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);

	return iiod_client_set_trigger(pdata->iiod_client, dev, trigger);
}

static struct iio_buffer_pdata *
serial_create_buffer(const struct iio_device *dev, unsigned int idx,
		     struct iio_channels_mask *mask)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);
	struct iio_buffer_pdata *buf;
	int ret;

	buf = zalloc(sizeof(*buf));
	if (!buf)
		return iio_ptr(-ENOMEM);

	buf->pdata = iiod_client_create_buffer(pdata->iiod_client,
					       dev, idx, mask);
	ret = iio_err(buf->pdata);
	if (ret) {
		dev_perror(dev, ret, "Unable to create IIOD client");
		free(buf);
		return iio_ptr(ret);
	}

	return buf;
}

static void serial_free_buffer(struct iio_buffer_pdata *buf)
{
	iiod_client_free_buffer(buf->pdata);
	free(buf);
}

static int serial_enable_buffer(struct iio_buffer_pdata *buf,
				size_t nb_samples, bool enable)
{
	return iiod_client_enable_buffer(buf->pdata, nb_samples, enable);
}

static struct iio_block_pdata *
serial_create_block(struct iio_buffer_pdata *buf, size_t size, void **data)
{
	return iiod_client_create_block(buf->pdata, size, data);
}

static struct iio_event_stream_pdata *
serial_open_events_fd(const struct iio_device *dev)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);

	return iiod_client_open_event_stream(pdata->iiod_client, dev);
}

static const struct iio_backend_ops serial_ops = {
	.create = serial_create_context_from_args,
	.read_attr = serial_read_attr,
	.write_attr = serial_write_attr,
	.shutdown = serial_shutdown,
	.get_trigger = serial_get_trigger,
	.set_trigger = serial_set_trigger,

	.create_buffer = serial_create_buffer,
	.free_buffer = serial_free_buffer,
	.enable_buffer = serial_enable_buffer,

	.create_block = serial_create_block,
	.free_block = iiod_client_free_block,
	.enqueue_block = iiod_client_enqueue_block,
	.dequeue_block = iiod_client_dequeue_block,

	.open_ev = serial_open_events_fd,
	.close_ev = iiod_client_close_event_stream,
	.read_ev = iiod_client_read_event,
};

__api_export_if(WITH_SERIAL_BACKEND_DYNAMIC)
const struct iio_backend iio_serial_backend = {
	.api_version = IIO_BACKEND_API_V1,
	.name = "serial",
	.uri_prefix = "serial:",
	.ops = &serial_ops,
	.default_timeout_ms = 1000,
};

static const struct iiod_client_ops serial_iiod_client_ops = {
	.write = serial_write_data,
	.read = serial_read_data,
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

static struct iio_context * serial_create_context(
		const struct iio_context_params *params, const char *port_name,
		unsigned int baud_rate, unsigned int bits, unsigned int stop,
		enum sp_parity parity, enum sp_flowcontrol flow)
{
	struct sp_port *port;
	struct iio_context_pdata *pdata;
	struct iio_context *ctx;
	const char *ctx_params[] = {
		"uri", "serial,port", "serial,description",
	};
	const char *ctx_params_values[ARRAY_SIZE(ctx_params)];
	char *uri, buf[16];
	size_t uri_len;
	int ret;

	uri_len = sizeof("serial:,1000000,8n1n") + strnlen(port_name, PATH_MAX);
	uri = malloc(uri_len);
	if (!uri)
		return iio_ptr(-ENOMEM);

	ret = libserialport_to_errno(sp_get_port_by_name(port_name, &port));
	if (ret)
		goto err_free_uri;

	ret = libserialport_to_errno(sp_open(port, SP_MODE_READ_WRITE));
	if (ret)
		goto err_free_port;

	ret = apply_settings(port, baud_rate, bits, stop, parity, flow);
	if (ret)
		goto err_close_port;

	/* Empty the output buffer */
	ret = libserialport_to_errno(sp_flush(port, SP_BUF_OUTPUT));
	if (ret)
		prm_warn(params, "Unable to flush output buffer\n");

	/* Drain the input buffer */
	do {
		ret = libserialport_to_errno(sp_blocking_read(port, buf,
							      sizeof(buf), 1));
		if (ret < 0) {
			prm_warn(params, "Unable to drain input buffer\n");
			break;
		}
	} while (ret);

	pdata = zalloc(sizeof(*pdata));
	if (!pdata) {
		ret = -ENOMEM;
		goto err_close_port;
	}

	pdata->port = port;
	pdata->params = *params;

	pdata->iiod_client = iiod_client_new(params,
					     (struct iiod_client_pdata *) pdata,
					     &serial_iiod_client_ops);
	ret = iio_err(pdata->iiod_client);
	if (ret)
		goto err_free_pdata;

	iio_snprintf(uri, uri_len, "serial:%s,%u,%u%c%u%c",
			port_name, baud_rate, bits,
			parity_char(parity), stop, flow_char(flow));

	ctx_params_values[0] = uri;
	ctx_params_values[1] = sp_get_port_name(port);
	ctx_params_values[2] = sp_get_port_description(port);

	ctx = iiod_client_create_context(pdata->iiod_client,
					 &iio_serial_backend, NULL,
					 ctx_params, ctx_params_values,
					 ARRAY_SIZE(ctx_params));
	ret = iio_err(ctx);
	if (ret)
		goto err_destroy_iiod_client;

	iio_context_set_pdata(ctx, pdata);
	free(uri);

	return ctx;

err_destroy_iiod_client:
	iiod_client_destroy(pdata->iiod_client);
err_free_pdata:
	free(pdata);
err_close_port:
	sp_close(port);
err_free_port:
	sp_free_port(port);
err_free_uri:
	free(uri);
	return iio_ptr(ret);
}

/* Take string, in "[baud rate],[data bits][parity][stop bits][flow control]"
 * notation, where:
 *   - baud_rate    = between 110 - 1,000,000 (default 115200)
 *   - data bits    = between 5 and 9 (default 8)
 *   - parity       = one of 'n' none, 'o' odd, 'e' even, 'm' mark, 's' space
 *                         (default 'n' none)
 *   - stop bits    = 1 or 2 (default 1)
 *   - flow control = one of '\0' none, 'x' Xon Xoff, 'r' RTSCTS, 'd' DTRDSR
 *                         (default '\0' none)
 *
 * eg: "115200,8n1x"
 *     "115200,8n1"
 *     "115200,8"
 *     "115200"
 *     ""
 */
static int serial_parse_options(const struct iio_context_params *params,
				const char *options, unsigned int *baud_rate,
				unsigned int *bits, unsigned int *stop,
				enum sp_parity *parity, enum sp_flowcontrol *flow)
{
	char *end, ch;
	unsigned int i;

	/* Default settings */
	*baud_rate = 115200;
	*parity = SP_PARITY_NONE;
	*bits = 8;
	*stop = 1;
	*flow = SP_FLOWCONTROL_NONE;

	if (!options || !options[0])
		return 0;

	errno = 0;
	*baud_rate = strtoul(options, &end, 10);
	if (options == end || errno == ERANGE)
		return -EINVAL;

	/* 110 baud to 1,000,000 baud */
	if (options == end || *baud_rate < 110 || *baud_rate > 4000000) {
		prm_err(params, "Invalid baud rate\n");
		return -EINVAL;
	}

	if (*end == ',')
		end++;

	if (!*end)
		return 0;

	options = (const char *)(end);

	errno = 0;
	*bits = strtoul(options, &end, 10);
	if (options == end || *bits > 9 || *bits < 5 || errno == ERANGE) {
		prm_err(params, "Invalid number of bits\n");
		return -EINVAL;
	}

	if (*end == ',')
		end++;

	if (*end == '\0')
		return 0;
	ch = (char)(tolower(*end) & 0xFF);
	for(i = 0; parity_options[i].flag != '\0'; i++) {
		if (ch == parity_options[i].flag) {
			*parity = parity_options[i].parity;
			break;
		}
	}
	if (parity_options[i].flag == '\0') {
		prm_err(params, "Invalid parity character\n");
		return -EINVAL;
	}

	end++;
	if (*end == ',')
		end++;

	options = (const char *)(end);

	if (!*options)
		return 0;

	options = (const char *)(end);
	if (!*options)
		return 0;

	errno = 0;
	*stop = strtoul(options, &end, 10);
	if (options == end || !*stop || *stop > 2 || errno == ERANGE) {
		prm_err(params, "Invalid number of stop bits\n");
		return -EINVAL;
	}

	if (*end == ',')
		end++;

	if (*end == '\0')
		return 0;
	ch = (char)(tolower(*end) & 0xFF);
	for(i = 0; flow_options[i].flag != '\0'; i++) {
		if (ch == flow_options[i].flag) {
			*flow = flow_options[i].flowcontrol;
			break;
		}
	}
	if (flow_options[i].flag == '\0') {
		prm_err(params, "Invalid flow control character\n");
		return -EINVAL;
	}

	/* We should have a '\0' after the flow character */
	if (end[1]) {
		prm_err(params, "Invalid characters after flow control flag\n");
		return -EINVAL;
	}

	return 0;
}

static struct iio_context *
serial_create_context_from_args(const struct iio_context_params *params,
				const char *args)
{
	struct iio_context *ctx = NULL;
	char *comma, *uri_dup;
	unsigned int baud_rate, bits, stop;
	enum sp_parity parity;
	enum sp_flowcontrol flow;
	int ret;

	uri_dup = iio_strdup(args);
	if (!uri_dup)
		return iio_ptr(-ENOMEM);

	comma = strchr(uri_dup, ',');
	if (comma) {
		*comma = '\0';
		ret = serial_parse_options(params, (char *)((uintptr_t) comma + 1),
					   &baud_rate, &bits, &stop,
					   &parity, &flow);
	} else {
		ret = serial_parse_options(params, NULL, &baud_rate, &bits,
					   &stop, &parity, &flow);
	}

	if (ret)
		goto err_free_dup;

	ctx = serial_create_context(params, uri_dup, baud_rate,
				    bits, stop, parity, flow);

	free(uri_dup);
	return ctx;

err_free_dup:
	free(uri_dup);
	prm_err(params, "Bad URI: \'serial:%s\'\n", args);
	return iio_ptr(-EINVAL);
}
