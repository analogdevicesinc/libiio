// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * ASCII (libiio v0.x) protocol command handlers shared by iiod and the
 * Zephyr tinyiiod server.
 *
 * Copyright (C) 2014-2026 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 *
 * NOTE: LGPL-2.1-or-later because this file holds command handlers extracted
 * from the LGPL-2.1 iiod/ops.c. To be relicensed to MIT together with
 * ops.c/ops.h/lexer.l/parser.y once that relicensing lands.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "iio/iio.h"
#include "ops.h"

#if WITH_LIBTINYIIOD
#define READ_ATTR_BUF_SIZE 1024
#else
#define READ_ATTR_BUF_SIZE 0x10000
#endif

void print_value(struct parser_pdata *pdata, long value)
{
	char buf[128];
	snprintf(buf, sizeof(buf), "%li\n", value);
	output(pdata, buf);
}

static inline uint32_t iiod_htobe32(uint32_t word)
{
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	return word;
#elif defined(__GNUC__)
	return __builtin_bswap32(word);
#else
	return ((word & 0xff) << 24) | ((word & 0xff00) << 8) | ((word >> 8) & 0xff00) |
	       ((word >> 24) & 0xff);
#endif
}

static inline uint32_t iiod_be32toh(uint32_t word)
{
	return iiod_htobe32(word);
}

typedef struct iio_attr *(*rw_attr_cb_t)(const void *, unsigned int);

static int buffer_analyze(unsigned int nb, const char *src, size_t len)
{
	while (nb--) {
		int32_t val;

		if (len < 4)
			return -EINVAL;

		val = (int32_t)iiod_be32toh(*(uint32_t *)src);
		src += 4;
		len -= 4;

		if (val > 0) {
			if ((uint32_t)val > len)
				return -EINVAL;

			/* Align the length to 4 bytes */
			if (val & 3)
				val = ((val >> 2) + 1) << 2;
			len -= val;
			src += val;
		}
	}

	/* We should have analyzed the whole buffer by now */
	return !len ? 0 : -EINVAL;
}

static ssize_t read_each_attr(
		const void *iio, char *buf, size_t len, unsigned int nb, rw_attr_cb_t cb)
{
	const struct iio_attr *attr;
	unsigned int i;
	char *ptr = buf;
	ssize_t ret;

	for (i = 0; len >= 4 && i < nb; i++) {
		attr = (*cb)(iio, i);
		if (!attr)
			ret = -ENOENT;
		else
			ret = iio_attr_read_raw(attr, ptr + 4, len - 4);
		*(uint32_t *)ptr = iiod_htobe32(ret);

		/* Align the length to 4 bytes */
		ret = ret < 0 ? 0 : (ret + 3) & ~0x3;
		ptr += 4 + ret;
		len -= 4 + ret;
	}

	return ptr - buf;
}

static ssize_t write_each_attr(
		const void *iio, const char *buf, size_t len, unsigned int nb, rw_attr_cb_t cb)
{
	const struct iio_attr *attr;
	const char *ptr = buf;
	unsigned int i;
	ssize_t ret;
	int32_t val;

	ret = buffer_analyze(nb, buf, len);
	if (ret < 0)
		return ret;

	for (i = 0; i < nb; i++) {
		val = (int32_t)iiod_be32toh(*(uint32_t *)ptr);
		ptr += 4;

		if (val > 0) {
			attr = (*cb)(iio, i);
			if (!attr)
				continue;

			iio_attr_write_raw(attr, ptr, val);

			/* Align the length to 4 bytes */
			ptr += (val + 3) & ~0x3;
		}
	}

	return ptr - buf;
}

ssize_t read_dev_attr(struct parser_pdata *pdata, struct iio_device *dev, const char *name,
		enum iio_attr_type type)
{
	const struct iio_attr *attr;
	struct iio_buffer *buffer;
	char *buf;
	ssize_t ret = -EINVAL;
	unsigned int nb;

	if (!dev) {
		print_value(pdata, -ENODEV);
		return -ENODEV;
	}

	buf = malloc(READ_ATTR_BUF_SIZE);
	if (!buf) {
		print_value(pdata, -ENOMEM);
		return -ENOMEM;
	}

	if (!name) {
		switch (type) {
		case IIO_ATTR_TYPE_DEVICE:
			nb = iio_device_get_attrs_count(dev);
			ret = read_each_attr(dev, buf, READ_ATTR_BUF_SIZE - 1, nb,
					(rw_attr_cb_t)iio_device_get_attr);
			break;
		case IIO_ATTR_TYPE_DEBUG:
			nb = iio_device_get_debug_attrs_count(dev);
			ret = read_each_attr(dev, buf, READ_ATTR_BUF_SIZE - 1, nb,
					(rw_attr_cb_t)iio_device_get_debug_attr);
			break;
		default:
			ret = -EINVAL;
			goto out_free_buffer;
		}

		goto out_print_value;
	}

	switch (type) {
	case IIO_ATTR_TYPE_DEVICE:
		attr = iio_device_find_attr(dev, name);
		if (attr)
			ret = iio_attr_read_raw(attr, buf, READ_ATTR_BUF_SIZE - 1);
		else
			ret = -ENOENT;
		break;
	case IIO_ATTR_TYPE_DEBUG:
		attr = iio_device_find_debug_attr(dev, name);
		if (attr)
			ret = iio_attr_read_raw(attr, buf, READ_ATTR_BUF_SIZE - 1);
		else
			ret = -ENOENT;
		break;
	case IIO_ATTR_TYPE_BUFFER:
		buffer = iio_device_get_buffer(dev, 0);
		if (buffer) {
			attr = iio_buffer_find_attr(buffer, name);
			if (attr)
				ret = iio_attr_read_raw(attr, buf, READ_ATTR_BUF_SIZE - 1);
			else
				ret = -ENOENT;
		} else {
			ret = -EBADF;
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

out_print_value:
	print_value(pdata, ret);
	if (ret < 0)
		goto out_free_buffer;

	buf[ret] = '\n';
	ret = write_all(pdata, buf, ret + 1);

out_free_buffer:
	free(buf);
	return ret;
}

ssize_t write_dev_attr(struct parser_pdata *pdata, struct iio_device *dev, const char *name,
		size_t len, enum iio_attr_type type)
{
	const struct iio_attr *attr;
	struct iio_buffer *buffer;
	unsigned int nb;
	ssize_t ret = -ENOMEM;
	char *buf;

	if (!dev) {
		ret = -ENODEV;
		goto out_print_value;
	}

	buf = malloc(len);
	if (!buf)
		goto out_print_value;

	ret = read_all(pdata, buf, len);
	if (ret < 0)
		goto out_free_buffer;

	if (!name) {
		switch (type) {
		case IIO_ATTR_TYPE_DEVICE:
			nb = iio_device_get_attrs_count(dev);
			ret = write_each_attr(
					dev, buf, len - 1, nb, (rw_attr_cb_t)iio_device_get_attr);
			break;
		case IIO_ATTR_TYPE_DEBUG:
			nb = iio_device_get_debug_attrs_count(dev);
			ret = write_each_attr(dev, buf, len - 1, nb,
					(rw_attr_cb_t)iio_device_get_debug_attr);
			break;
		default:
			ret = -EINVAL;
			goto out_free_buffer;
		}

		goto out_free_buffer;
	}

	switch (type) {
	case IIO_ATTR_TYPE_DEVICE:
		attr = iio_device_find_attr(dev, name);
		if (attr)
			ret = iio_attr_write_raw(attr, buf, len);
		else
			ret = -ENOENT;
		break;
	case IIO_ATTR_TYPE_DEBUG:
		attr = iio_device_find_debug_attr(dev, name);
		if (attr)
			ret = iio_attr_write_raw(attr, buf, len);
		else
			ret = -ENOENT;
		break;
	case IIO_ATTR_TYPE_BUFFER:
		buffer = iio_device_get_buffer(dev, 0);
		if (buffer) {
			attr = iio_buffer_find_attr(buffer, name);
			if (attr)
				ret = iio_attr_write_raw(attr, buf, len);
			else
				ret = -ENOENT;
		} else {
			ret = -EBADF;
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

out_free_buffer:
	free(buf);
out_print_value:
	print_value(pdata, ret);
	return ret;
}

ssize_t read_chn_attr(struct parser_pdata *pdata, struct iio_channel *chn, const char *name)
{
	char *buf;
	ssize_t ret = -ENODEV;
	const struct iio_attr *attr;
	unsigned int nb;

	if (!chn) {
		ret = pdata->dev ? -ENXIO : -ENODEV;
		print_value(pdata, ret);
		return ret;
	}

	buf = malloc(READ_ATTR_BUF_SIZE);
	if (!buf) {
		print_value(pdata, -ENOMEM);
		return -ENOMEM;
	}

	if (!name) {
		nb = iio_channel_get_attrs_count(chn);
		ret = read_each_attr(chn, buf, READ_ATTR_BUF_SIZE - 1, nb,
				(rw_attr_cb_t)iio_channel_get_attr);
	} else {
		attr = iio_channel_find_attr(chn, name);
		if (attr)
			ret = iio_attr_read_raw(attr, buf, READ_ATTR_BUF_SIZE - 1);
		else
			ret = -ENOENT;
	}

	print_value(pdata, ret);
	if (ret < 0)
		goto out_free_buffer;

	buf[ret] = '\n';
	ret = write_all(pdata, buf, ret + 1);

out_free_buffer:
	free(buf);
	return ret;
}

ssize_t write_chn_attr(
		struct parser_pdata *pdata, struct iio_channel *chn, const char *name, size_t len)
{
	const struct iio_attr *attr;
	ssize_t ret = -ENOMEM;
	unsigned int nb;
	char *buf;

	buf = malloc(len);
	if (!buf)
		goto err_print_value;

	ret = read_all(pdata, buf, len);
	if (ret < 0)
		goto err_free_buffer;

	if (!chn) {
		ret = pdata->dev ? -ENXIO : -ENODEV;
		goto err_free_buffer;
	}

	if (!name) {
		nb = iio_channel_get_attrs_count(chn);
		ret = write_each_attr(chn, buf, len - 1, nb, (rw_attr_cb_t)iio_channel_get_attr);
	} else {
		attr = iio_channel_find_attr(chn, name);
		if (attr)
			ret = iio_attr_write_raw(attr, buf, len);
		else
			ret = -ENOENT;
	}

err_free_buffer:
	free(buf);
err_print_value:
	print_value(pdata, ret);
	return ret;
}

ssize_t set_trigger(struct parser_pdata *pdata, struct iio_device *dev, const char *trigger)
{
	struct iio_device *trig = NULL;
	ssize_t ret = -ENOENT;

	if (!dev) {
		ret = -ENODEV;
		goto err_print_value;
	}

	if (trigger) {
		trig = iio_context_find_device(pdata->ctx, trigger);
		if (!trig)
			goto err_print_value;
	}

	ret = iio_device_set_trigger(dev, trig);
err_print_value:
	print_value(pdata, ret);
	return ret;
}

ssize_t get_trigger(struct parser_pdata *pdata, struct iio_device *dev)
{
	const struct iio_device *trigger;
	ssize_t ret;

	if (!dev) {
		print_value(pdata, -ENODEV);
		return -ENODEV;
	}

	trigger = iio_device_get_trigger(dev);
	ret = iio_err(trigger);
	if (!ret) {
		const char *name = iio_device_get_name(trigger);
		char buf[256];

		ret = strlen(name);
		print_value(pdata, ret);

		snprintf(buf, sizeof(buf), "%s\n", name);
		ret = write_all(pdata, buf, ret + 1);
	} else {
		print_value(pdata, ret);
	}
	return ret;
}

int set_timeout(struct parser_pdata *pdata, int timeout)
{
	int translated_timeout = timeout;
	int ret;

	/* Translate v0 client timeout semantics to v1 semantics:
	 * - v0 clients (non-binary) use: 0 = infinite, positive = timeout
	 * - v1 clients (binary) use: -1 = infinite, 0 = backend default, positive = timeout
	 */
	if (!pdata->binary && timeout == 0) {
		/* v0 client sending 0 (infinite) -> translate to v1 infinite (-1) */
		translated_timeout = -1;
	}

	ret = iio_context_set_timeout(pdata->ctx, translated_timeout);
	print_value(pdata, ret);
	return ret;
}

void enable_binary(struct parser_pdata *pdata)
{
	pdata->binary = true;

	print_value(pdata, 0);
}
