// SPDX-License-Identifier: MIT
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * ASCII (libiio v0.x) protocol command handlers for the Zephyr iiod server.
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "iio/iio.h"

#include "../iiod/ops.h"
#include "../iiod/parser.h"

int yyparse(yyscan_t scanner);

/* Identical to print_value() in ops.c. */
static void print_value(struct parser_pdata *pdata, long value)
{
	char buf[128];
	snprintf(buf, sizeof(buf), "%li\n", value);
	output(pdata, buf);
}

/* Identical to iiod_htobe32() in ops.c. */
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

/* Identical to iiod_be32toh() in ops.c. */
static inline uint32_t iiod_be32toh(uint32_t word)
{
	return iiod_htobe32(word);
}

#define READ_ATTR_BUF_SIZE 1024

typedef struct iio_attr *(*rw_attr_cb_t)(const void *, unsigned int);

/* Identical to buffer_analyze() in ops.c. */
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

/* Identical to read_each_attr() in ops.c. */
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

/* Identical to write_each_attr() in ops.c. */
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

/* Functionally identical to read_dev_attr() in ops.c, except the value buffer
 * is heap-allocated (READ_ATTR_BUF_SIZE) instead of a large on-stack array */
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

/* Functionally identical to write_dev_attr() in ops.c. Differs only on the
 * unsupported-attr-type path, where the allocated buffer is freed (via goto)
 * instead of returned past, avoiding a leak. TODO - check supposed leak */
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

/* Functionally identical to read_chn_attr() in ops.c, except the value buffer
 * is heap-allocated (READ_ATTR_BUF_SIZE) instead of a large on-stack array */
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

/* Identical to write_chn_attr() in ops.c. */
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
		ret = write_each_attr(
				chn, buf, sizeof(buf) - 1, nb, (rw_attr_cb_t)iio_channel_get_attr);
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

/* Identical to set_trigger() in ops.c. */
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

/* Identical to get_trigger() in ops.c. */
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

/* Identical to set_timeout() in ops.c. */
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

/* Stub - to be implemented. */
int set_buffers_count(struct parser_pdata *pdata, struct iio_device *dev, long value)
{
	(void)dev;
	(void)value;

	print_value(pdata, -ENOSYS);
	return -ENOSYS;
}

/* Stub - to be implemented. */
int open_dev(struct parser_pdata *pdata, struct iio_device *dev, size_t samples_count,
		const char *mask, bool cyclic)
{
	(void)dev;
	(void)samples_count;
	(void)mask;
	(void)cyclic;

	print_value(pdata, -ENOSYS);
	return -ENOSYS;
}

/* Stub - to be implemented. */
int close_dev(struct parser_pdata *pdata, struct iio_device *dev)
{
	(void)pdata;
	(void)dev;

	/* No device can be open yet (open_dev fails), so this is only reached
	 * via the explicit CLOSE command. Reply quietly with success to keep the
	 * client's state machine happy. TODO: check flow reaching this point*/
	return 0;
}

/* Stub - to be implemented. */
ssize_t rw_dev(struct parser_pdata *pdata, struct iio_device *dev, unsigned int nb, bool is_write)
{
	(void)dev;
	(void)nb;
	(void)is_write;

	print_value(pdata, -ENOSYS);
	return -ENOSYS;
}

/*
 * Differs from invalidate_sample_size_cache() in ops.c: reimplemented as a
 * no-op. Called from responder.c (binary path) when a channel's format changes.
 * The ops.c version signals the per-device RW thread to recompute the sample
 * size; there is no such thread here, so nothing needs to be done. Still
 * provided because responder.c references it whenever WITH_IIOD_V0_COMPAT is
 * set, so the symbol must exist at link time.
 */
void invalidate_sample_size_cache(const struct iio_device *dev)
{
	(void)dev;
}

/* Differs from read_line() in ops.c: reimplemented with only the byte-at-a-time
 * path. ops.c also has a socket branch (recv() with MSG_PEEK/MSG_TRUNC) and a
 * USB bulk branch, both of which can read past the newline; that would corrupt
 * the ASCII->binary handoff on the Zephyr transports. Here we read one byte at
 * a time until the newline, so nothing past the line is ever consumed. */
ssize_t read_line(struct parser_pdata *pdata, char *buf, size_t len)
{
	size_t bytes_read = 0;
	bool found;

	while (len) {
		ssize_t ret = pdata->readfd(pdata, buf, 1);
		if (ret <= 0)
			return ret;

		bytes_read++;

		if (*buf == '\n')
			break;

		len--;
		buf++;
	}

	found = !!len;

	return found ? (ssize_t)bytes_read : -EIO;
}

/* Identical to enable_binary() in ops.c. */
void enable_binary(struct parser_pdata *pdata)
{
	pdata->binary = true;

	print_value(pdata, 0);
}

/* Differs from ascii_interpreter() in ops.c: the yylex/yyparse loop is the
 * same, but the trailing per-device cleanup loop (close_dev_helper() over every
 * device) is omitted. That loop tears down the POSIX RW threads, which do not
 * exist here; no device can be open yet, so there is nothing to tear down. */
void ascii_interpreter(struct parser_pdata *pdata)
{
	yyscan_t scanner;
	int ret;

	yylex_init_extra(pdata, &scanner);

	do {
		ret = yyparse(scanner);
	} while (!pdata->stop && !pdata->binary && ret >= 0);

	yylex_destroy(scanner);
}
