// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014-2016 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "iio-debug.h"
#include "iio-config.h"
#include "iio-lock.h"
#include "iiod-client.h"

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

struct iio_device_pdata {
	bool opened;

	struct iiod_client_io *client_io;
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

static char * serial_get_description(struct sp_port *port)
{
	char *description, *name, *desc;
	size_t desc_len;

	name = sp_get_port_name(port);
	desc = sp_get_port_description(port);

	desc_len = sizeof(": \0") + strlen(name) + strlen(desc);
	description = malloc(desc_len);
	if (!description) {
		errno = ENOMEM;
		return NULL;
	}

	iio_snprintf(description, desc_len, "%s: %s", name, desc);

	return description;
}

static int serial_open(const struct iio_device *dev,
		size_t samples_count, bool cyclic)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *ctx_pdata = iio_context_get_pdata(ctx);
	struct iio_device_pdata *pdata = iio_device_get_pdata(dev);
	int ret = -EBUSY;

	iiod_client_mutex_lock(ctx_pdata->iiod_client);
	if (pdata->opened)
		goto out_unlock;

	pdata->client_io = iiod_client_open_unlocked(ctx_pdata->iiod_client,
						     dev, samples_count,
						     cyclic);
	ret = PTR_ERR_OR_ZERO(pdata->client_io);
	pdata->opened = !ret;

out_unlock:
	iiod_client_mutex_unlock(ctx_pdata->iiod_client);
	return ret;
}

static int serial_close(const struct iio_device *dev)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *ctx_pdata = iio_context_get_pdata(ctx);
	struct iio_device_pdata *pdata = iio_device_get_pdata(dev);
	int ret = -EBADF;

	iiod_client_mutex_lock(ctx_pdata->iiod_client);
	if (!pdata->opened)
		goto out_unlock;

	ret = iiod_client_close_unlocked(pdata->client_io);
	pdata->opened = false;

out_unlock:
	iiod_client_mutex_unlock(ctx_pdata->iiod_client);
	return ret;
}

static ssize_t serial_read(const struct iio_device *dev, void *dst, size_t len,
		uint32_t *mask, size_t words)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);

	return iiod_client_read(pdata->iiod_client, dev, dst, len, mask, words);
}

static ssize_t serial_write(const struct iio_device *dev,
		const void *src, size_t len)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);

	return iiod_client_write(pdata->iiod_client, dev, src, len);
}

static ssize_t serial_read_dev_attr(const struct iio_device *dev,
		const char *attr, char *dst, size_t len, enum iio_attr_type type)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);

	return iiod_client_read_attr(pdata->iiod_client,
				     dev, NULL, attr, dst, len, type);
}

static ssize_t serial_write_dev_attr(const struct iio_device *dev,
		const char *attr, const char *src, size_t len, enum iio_attr_type type)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);

	return iiod_client_write_attr(pdata->iiod_client,
				      dev, NULL, attr, src, len, type);
}

static ssize_t serial_read_chn_attr(const struct iio_channel *chn,
		const char *attr, char *dst, size_t len)
{
	const struct iio_device *dev = iio_channel_get_device(chn);
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);

	return iiod_client_read_attr(pdata->iiod_client,
				     dev, chn, attr, dst, len, false);
}

static ssize_t serial_write_chn_attr(const struct iio_channel *chn,
		const char *attr, const char *src, size_t len)
{
	const struct iio_device *dev = iio_channel_get_device(chn);
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);

	return iiod_client_write_attr(pdata->iiod_client,
				      dev, chn, attr, src, len, false);
}

static int serial_set_kernel_buffers_count(const struct iio_device *dev,
		unsigned int nb_blocks)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);

	return iiod_client_set_kernel_buffers_count(pdata->iiod_client,
						    dev, nb_blocks);
}

static ssize_t serial_write_data(struct iiod_client_pdata *io_data,
				 const char *data, size_t len)
{
	struct iio_context_pdata *pdata = (struct iio_context_pdata *) io_data;
	unsigned int timeout_ms = pdata->params.timeout_ms;
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
				char *buf, size_t len)
{
	struct iio_context_pdata *pdata = (struct iio_context_pdata *) io_data;
	unsigned int timeout_ms = pdata->params.timeout_ms;
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

static ssize_t serial_read_line(struct iiod_client_pdata *io_data,
				char *buf, size_t len)
{
	struct iio_context_pdata *pdata = (struct iio_context_pdata *) io_data;
	unsigned int timeout_ms = pdata->params.timeout_ms;
	enum sp_return sp_ret;
	size_t i;
	bool found = false;
	int ret;

	prm_dbg(&pdata->params, "Readline size 0x%lx\n", (unsigned long) len);

	for (i = 0; i < len - 1; i++) {
		sp_ret = sp_blocking_read_next(pdata->port, &buf[i], 1, timeout_ms);

		ret = libserialport_to_errno(sp_ret);
		if (ret == 0) {
			prm_err(&pdata->params, "sp_blocking_read_next has timed out\n");
			return -ETIMEDOUT;
		}

		if (ret < 0) {
			prm_err(&pdata->params, "sp_blocking_read_next returned %i\n",
				ret);
			return (ssize_t) ret;
		}

		prm_dbg(&pdata->params, "Character: %c\n", buf[i]);

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
	struct iio_context_pdata *ctx_pdata = iio_context_get_pdata(ctx);
	unsigned int i;

	iiod_client_destroy(ctx_pdata->iiod_client);
	sp_close(ctx_pdata->port);
	sp_free_port(ctx_pdata->port);

	for (i = 0; i < iio_context_get_devices_count(ctx); i++) {
		const struct iio_device *dev = iio_context_get_device(ctx, i);
		struct iio_device_pdata *pdata = iio_device_get_pdata(dev);

		free(pdata);
	}
}

static int serial_set_timeout(struct iio_context *ctx, unsigned int timeout)
{
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);

	pdata->params.timeout_ms = timeout;

	return 0;
}

static int serial_get_trigger(const struct iio_device *dev,
			      const struct iio_device **trigger)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);

	return iiod_client_get_trigger(pdata->iiod_client, dev, trigger);
}

static int serial_set_trigger(const struct iio_device *dev,
			      const struct iio_device *trigger)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);

	return iiod_client_set_trigger(pdata->iiod_client, dev, trigger);
}

static const struct iio_backend_ops serial_ops = {
	.create = serial_create_context_from_args,
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
	.get_trigger = serial_get_trigger,
	.set_trigger = serial_set_trigger,
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

static struct iio_context * serial_create_context(
		const struct iio_context_params *params, const char *port_name,
		unsigned int baud_rate, unsigned int bits, unsigned int stop,
		enum sp_parity parity, enum sp_flowcontrol flow)
{
	struct sp_port *port;
	struct iio_context_pdata *pdata;
	struct iio_context *ctx;
	const char *ctx_uri = "uri";
	char *description;
	size_t uri_len;
	unsigned int i;
	int ret;
	char *uri;

	uri_len = sizeof("serial:,1000000,8n1n") + strnlen(port_name, PATH_MAX);
	uri = malloc(uri_len);
	if (!uri) {
		errno = ENOMEM;
		return NULL;
	}

	ret = libserialport_to_errno(sp_get_port_by_name(port_name, &port));
	if (ret) {
		errno = -ret;
		goto err_free_uri;
	}

	ret = libserialport_to_errno(sp_open(port, SP_MODE_READ_WRITE));
	if (ret) {
		errno = -ret;
		goto err_free_port;
	}

	ret = apply_settings(port, baud_rate, bits, stop, parity, flow);
	if (ret) {
		errno = -ret;
		goto err_close_port;
	}

	/* Empty the buffers */
	sp_flush(port, SP_BUF_BOTH);

	description = serial_get_description(port);
	if (!description)
		goto err_close_port;

	pdata = zalloc(sizeof(*pdata));
	if (!pdata) {
		errno = ENOMEM;
		goto err_free_description;
	}

	pdata->port = port;
	pdata->params = *params;

	pdata->iiod_client = iiod_client_new(params,
					     (struct iiod_client_pdata *) pdata,
					     &serial_iiod_client_ops);
	if (!pdata->iiod_client)
		goto err_free_pdata;

	iio_snprintf(uri, uri_len, "serial:%s,%u,%u%c%u%c",
			port_name, baud_rate, bits,
			parity_char(parity), stop, flow_char(flow));

	ctx = iiod_client_create_context(pdata->iiod_client,
					 &iio_serial_backend, description,
					 &ctx_uri, (const char **)&uri, 1);
	if (!ctx)
		goto err_destroy_iiod_client;

	iio_context_set_pdata(ctx, pdata);

	for (i = 0; i < iio_context_get_devices_count(ctx); i++) {
		struct iio_device *dev = iio_context_get_device(ctx, i);
		struct iio_device_pdata *ppdata;

		ppdata = zalloc(sizeof(*ppdata));
		if (!ppdata) {
			ret = -ENOMEM;
			goto err_context_destroy;
		}

		iio_device_set_pdata(dev, ppdata);
	}

	free(uri);

	return ctx;

err_context_destroy:
	free(uri);
	iio_context_destroy(ctx);
	errno = -ret;
	return NULL;

err_destroy_iiod_client:
	iiod_client_destroy(pdata->iiod_client);
err_free_pdata:
	free(pdata);
err_free_description:
	free(description);
err_close_port:
	sp_close(port);
err_free_port:
	sp_free_port(port);
err_free_uri:
	free(uri);
	return NULL;
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
	if (!uri_dup) {
		errno = ENOMEM;
		return NULL;
	}

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
	errno = EINVAL;
	return NULL;
}
