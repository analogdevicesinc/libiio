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

#include "debug.h"
#include "iio-private.h"

#include <inttypes.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static char *get_attr_xml(const char *attr, size_t *length, bool is_debug)
{
	size_t len = sizeof("<attribute name=\"\" />") + strlen(attr)
		+ (!is_debug ? 0 : sizeof("debug-") - 1);
	char *str = malloc(len);
	if (!str)
		return NULL;

	*length = len - 1; /* Skip the \0 */
	if (is_debug)
		iio_snprintf(str, len, "<debug-attribute name=\"%s\" />", attr);
	else
		iio_snprintf(str, len, "<attribute name=\"%s\" />", attr);
	return str;
}

/* Returns a string containing the XML representation of this device */
char * iio_device_get_xml(const struct iio_device *dev, size_t *length)
{
	size_t len = sizeof("<device id=\"\" name=\"\" ></device>")
		+ strlen(dev->id) + (dev->name ? strlen(dev->name) : 0);
	char *ptr, *str, **attrs, **channels, **debug_attrs;
	size_t *attrs_len, *channels_len, *debug_attrs_len;
	unsigned int i, j, k;

	attrs_len = malloc(dev->nb_attrs * sizeof(*attrs_len));
	if (!attrs_len)
		return NULL;

	attrs = malloc(dev->nb_attrs * sizeof(*attrs));
	if (!attrs)
		goto err_free_attrs_len;

	for (i = 0; i < dev->nb_attrs; i++) {
		char *xml = get_attr_xml(dev->attrs[i], &attrs_len[i], false);
		if (!xml)
			goto err_free_attrs;
		attrs[i] = xml;
		len += attrs_len[i];
	}

	channels_len = malloc(dev->nb_channels * sizeof(*channels_len));
	if (!channels_len)
		goto err_free_attrs;

	channels = malloc(dev->nb_channels * sizeof(*channels));
	if (!channels)
		goto err_free_channels_len;

	for (j = 0; j < dev->nb_channels; j++) {
		char *xml = iio_channel_get_xml(dev->channels[j],
				&channels_len[j]);
		if (!xml)
			goto err_free_channels;
		channels[j] = xml;
		len += channels_len[j];
	}

	debug_attrs_len = malloc(dev->nb_debug_attrs *
			sizeof(*debug_attrs_len));
	if (!debug_attrs_len)
		goto err_free_channels;

	debug_attrs = malloc(dev->nb_debug_attrs * sizeof(*debug_attrs));
	if (!debug_attrs)
		goto err_free_debug_attrs_len;

	for (k = 0; k < dev->nb_debug_attrs; k++) {
		char *xml = get_attr_xml(dev->debug_attrs[k],
				&debug_attrs_len[k], true);
		if (!xml)
			goto err_free_debug_attrs;
		debug_attrs[k] = xml;
		len += debug_attrs_len[k];
	}

	str = malloc(len);
	if (!str)
		goto err_free_debug_attrs;

	iio_snprintf(str, len, "<device id=\"%s\"", dev->id);
	ptr = strrchr(str, '\0');

	if (dev->name) {
		sprintf(ptr, " name=\"%s\"", dev->name);
		ptr = strrchr(ptr, '\0');
	}

	strcpy(ptr, " >");
	ptr += 2;

	for (i = 0; i < dev->nb_channels; i++) {
		strcpy(ptr, channels[i]);
		ptr += channels_len[i];
		free(channels[i]);
	}

	free(channels);
	free(channels_len);

	for (i = 0; i < dev->nb_attrs; i++) {
		strcpy(ptr, attrs[i]);
		ptr += attrs_len[i];
		free(attrs[i]);
	}

	free(attrs);
	free(attrs_len);

	for (i = 0; i < dev->nb_debug_attrs; i++) {
		strcpy(ptr, debug_attrs[i]);
		ptr += debug_attrs_len[i];
		free(debug_attrs[i]);
	}

	free(debug_attrs);
	free(debug_attrs_len);

	strcpy(ptr, "</device>");
	*length = ptr - str + sizeof("</device>") - 1;
	return str;

err_free_debug_attrs:
	while (k--)
		free(debug_attrs[k]);
	free(debug_attrs);
err_free_debug_attrs_len:
	free(debug_attrs_len);
err_free_channels:
	while (j--)
		free(channels[j]);
	free(channels);
err_free_channels_len:
	free(channels_len);
err_free_attrs:
	while (i--)
		free(attrs[i]);
	free(attrs);
err_free_attrs_len:
	free(attrs_len);
	return NULL;
}

const char * iio_device_get_id(const struct iio_device *dev)
{
	return dev->id;
}

const char * iio_device_get_name(const struct iio_device *dev)
{
	return dev->name;
}

unsigned int iio_device_get_channels_count(const struct iio_device *dev)
{
	return dev->nb_channels;
}

struct iio_channel * iio_device_get_channel(const struct iio_device *dev,
		unsigned int index)
{
	if (index >= dev->nb_channels)
		return NULL;
	else
		return dev->channels[index];
}

struct iio_channel * iio_device_find_channel(const struct iio_device *dev,
		const char *name, bool output)
{
	unsigned int i;
	for (i = 0; i < dev->nb_channels; i++) {
		struct iio_channel *chn = dev->channels[i];
		if (iio_channel_is_output(chn) != output)
			continue;

		if (!strcmp(chn->id, name) ||
				(chn->name && !strcmp(chn->name, name)))
			return chn;
	}
	return NULL;
}

unsigned int iio_device_get_attrs_count(const struct iio_device *dev)
{
	return dev->nb_attrs;
}

const char * iio_device_get_attr(const struct iio_device *dev,
		unsigned int index)
{
	if (index >= dev->nb_attrs)
		return NULL;
	else
		return dev->attrs[index];
}

const char * iio_device_find_attr(const struct iio_device *dev,
		const char *name)
{
	unsigned int i;
	for (i = 0; i < dev->nb_attrs; i++) {
		const char *attr = dev->attrs[i];
		if (!strcmp(attr, name))
			return attr;
	}
	return NULL;
}

const char * iio_device_find_debug_attr(const struct iio_device *dev,
		const char *name)
{
	unsigned int i;
	for (i = 0; i < dev->nb_debug_attrs; i++) {
		const char *attr = dev->debug_attrs[i];
		if (!strcmp(attr, name))
			return attr;
	}
	return NULL;
}

bool iio_device_is_tx(const struct iio_device *dev)
{
	unsigned int i;

	for (i = 0; i < dev->nb_channels; i++) {
		struct iio_channel *ch = dev->channels[i];
		if (iio_channel_is_output(ch) && iio_channel_is_enabled(ch))
			return true;
	}

	return false;
}

int iio_device_open(const struct iio_device *dev,
		size_t samples_count, bool cyclic)
{
	unsigned int i;
	bool has_channels = false;

	for (i = 0; !has_channels && i < dev->words; i++)
		has_channels = !!dev->mask[i];
	if (!has_channels)
		return -EINVAL;

	if (dev->ctx->ops->open)
		return dev->ctx->ops->open(dev, samples_count, cyclic);
	else
		return -ENOSYS;
}

int iio_device_close(const struct iio_device *dev)
{
	if (dev->ctx->ops->close)
		return dev->ctx->ops->close(dev);
	else
		return -ENOSYS;
}

int iio_device_get_poll_fd(const struct iio_device *dev)
{
	if (dev->ctx->ops->get_fd)
		return dev->ctx->ops->get_fd(dev);
	else
		return -ENOSYS;
}

bool iio_device_get_blocking_mode(const struct iio_device *dev)
{
	if (dev->ctx->ops->get_blocking_mode)
		return dev->ctx->ops->get_blocking_mode(dev);
	else
		return true;
}

int iio_device_set_blocking_mode(const struct iio_device *dev, bool blocking)
{
	if (dev->ctx->ops->set_blocking_mode)
		return dev->ctx->ops->set_blocking_mode(dev, blocking);
	else
		return -ENOSYS;
}

ssize_t iio_device_read_raw(const struct iio_device *dev,
		void *dst, size_t len, uint32_t *mask, size_t words)
{
	if (dev->ctx->ops->read)
		return dev->ctx->ops->read(dev, dst, len, mask, words);
	else
		return -ENOSYS;
}

ssize_t iio_device_write_raw(const struct iio_device *dev,
		const void *src, size_t len)
{
	if (dev->ctx->ops->write)
		return dev->ctx->ops->write(dev, src, len);
	else
		return -ENOSYS;
}

ssize_t iio_device_attr_read(const struct iio_device *dev,
		const char *attr, char *dst, size_t len)
{
	if (dev->ctx->ops->read_device_attr)
		return dev->ctx->ops->read_device_attr(dev,
				attr, dst, len, false);
	else
		return -ENOSYS;
}

ssize_t iio_device_attr_write_raw(const struct iio_device *dev,
		const char *attr, const void *src, size_t len)
{
	if (dev->ctx->ops->write_device_attr)
		return dev->ctx->ops->write_device_attr(dev,
				attr, src, len, false);
	else
		return -ENOSYS;
}

ssize_t iio_device_attr_write(const struct iio_device *dev,
		const char *attr, const char *src)
{
	return iio_device_attr_write_raw(dev, attr, src, strlen(src) + 1);
}

void iio_device_set_data(struct iio_device *dev, void *data)
{
	dev->userdata = data;
}

void * iio_device_get_data(const struct iio_device *dev)
{
	return dev->userdata;
}

bool iio_device_is_trigger(const struct iio_device *dev)
{
	/* A trigger has a name, an id which starts by "trigger",
	 * and zero channels. */

	unsigned int nb = iio_device_get_channels_count(dev);
	const char *name = iio_device_get_name(dev),
	      *id = iio_device_get_id(dev);
	return ((nb == 0) && !!name &&
		!strncmp(id, "trigger", sizeof("trigger") - 1));
}

int iio_device_set_kernel_buffers_count(const struct iio_device *dev,
		unsigned int nb_buffers)
{
	if (nb_buffers == 0)
		return -EINVAL;
	else if (dev->ctx->ops->set_kernel_buffers_count)
		return dev->ctx->ops->set_kernel_buffers_count(dev, nb_buffers);
	else
		return -ENOSYS;
}

int iio_device_get_trigger(const struct iio_device *dev,
		const struct iio_device **trigger)
{
	if (!trigger)
		return -EINVAL;
	else if (dev->ctx->ops->get_trigger)
		return dev->ctx->ops->get_trigger(dev, trigger);
	else
		return -ENOSYS;
}

int iio_device_set_trigger(const struct iio_device *dev,
		const struct iio_device *trigger)
{
	if (trigger && !iio_device_is_trigger(trigger))
		return -EINVAL;
	else if (dev->ctx->ops->set_trigger)
		return dev->ctx->ops->set_trigger(dev, trigger);
	else
		return -ENOSYS;
}

void free_device(struct iio_device *dev)
{
	unsigned int i;
	for (i = 0; i < dev->nb_attrs; i++)
		free(dev->attrs[i]);
	if (dev->nb_attrs)
		free(dev->attrs);
	for (i = 0; i < dev->nb_debug_attrs; i++)
		free(dev->debug_attrs[i]);
	if (dev->nb_debug_attrs)
		free(dev->debug_attrs);
	for (i = 0; i < dev->nb_channels; i++)
		free_channel(dev->channels[i]);
	if (dev->nb_channels)
		free(dev->channels);
	if (dev->mask)
		free(dev->mask);
	if (dev->name)
		free(dev->name);
	if (dev->id)
		free(dev->id);
	free(dev);
}

ssize_t iio_device_get_sample_size_mask(const struct iio_device *dev,
		const uint32_t *mask, size_t words)
{
	ssize_t size = 0;
	unsigned int i;

	if (words != (dev->nb_channels + 31) / 32)
		return -EINVAL;

	for (i = 0; i < dev->nb_channels; i++) {
		const struct iio_channel *chn = dev->channels[i];
		unsigned int length = chn->format.length / 8 *
			chn->format.repeat;

		if (chn->index < 0)
			break;
		if (!TEST_BIT(mask, chn->index))
			continue;
		if (i > 0 && chn->index == dev->channels[i - 1]->index)
			continue;

		if (size % length)
			size += 2 * length - (size % length);
		else
			size += length;
	}
	return size;
}

ssize_t iio_device_get_sample_size(const struct iio_device *dev)
{
	return iio_device_get_sample_size_mask(dev, dev->mask, dev->words);
}

int iio_device_attr_read_longlong(const struct iio_device *dev,
		const char *attr, long long *val)
{
	char *end, buf[1024];
	long long value;
	ssize_t ret = iio_device_attr_read(dev, attr, buf, sizeof(buf));
	if (ret < 0)
		return (int) ret;

	value = strtoll(buf, &end, 0);
	if (end == buf)
		return -EINVAL;
	*val = value;
	return 0;
}

int iio_device_attr_read_bool(const struct iio_device *dev,
		const char *attr, bool *val)
{
	long long value;
	int ret = iio_device_attr_read_longlong(dev, attr, &value);
	if (ret < 0)
		return ret;

	*val = !!value;
	return 0;
}

int iio_device_attr_read_double(const struct iio_device *dev,
		const char *attr, double *val)
{
	char buf[1024];
	ssize_t ret = iio_device_attr_read(dev, attr, buf, sizeof(buf));
	if (ret < 0)
		return (int) ret;
	else
		return read_double(buf, val);
}

int iio_device_attr_write_longlong(const struct iio_device *dev,
		const char *attr, long long val)
{
	ssize_t ret;
	char buf[1024];

	iio_snprintf(buf, sizeof(buf), "%lld", val);
	ret = iio_device_attr_write(dev, attr, buf);

	return ret < 0 ? ret : 0;
}

int iio_device_attr_write_double(const struct iio_device *dev,
		const char *attr, double val)
{
	ssize_t ret;
	char buf[1024];

	ret = (ssize_t) write_double(buf, sizeof(buf), val);
	if (!ret)
		ret = iio_device_attr_write(dev, attr, buf);
	return ret < 0 ? ret : 0;
}

int iio_device_attr_write_bool(const struct iio_device *dev,
		const char *attr, bool val)
{
	ssize_t ret;

	if (val)
		ret = iio_device_attr_write(dev, attr, "1");
	else
		ret = iio_device_attr_write(dev, attr, "0");

	return ret < 0 ? ret : 0;
}

ssize_t iio_device_debug_attr_read(const struct iio_device *dev,
		const char *attr, char *dst, size_t len)
{
	if (dev->ctx->ops->read_device_attr)
		return dev->ctx->ops->read_device_attr(dev,
				attr, dst, len, true);
	else
		return -ENOSYS;
}

ssize_t iio_device_debug_attr_write_raw(const struct iio_device *dev,
		const char *attr, const void *src, size_t len)
{
	if (dev->ctx->ops->write_device_attr)
		return dev->ctx->ops->write_device_attr(dev,
				attr, src, len, true);
	else
		return -ENOSYS;
}

ssize_t iio_device_debug_attr_write(const struct iio_device *dev,
		const char *attr, const char *src)
{
	return iio_device_debug_attr_write_raw(dev, attr, src, strlen(src) + 1);
}

unsigned int iio_device_get_debug_attrs_count(const struct iio_device *dev)
{
	return dev->nb_debug_attrs;
}

const char * iio_device_get_debug_attr(const struct iio_device *dev,
		unsigned int index)
{
	if (index >= dev->nb_debug_attrs)
		return NULL;
	else
		return dev->debug_attrs[index];
}

int iio_device_debug_attr_read_longlong(const struct iio_device *dev,
		const char *attr, long long *val)
{
	char *end, buf[1024];
	long long value;
	ssize_t ret = iio_device_debug_attr_read(dev, attr, buf, sizeof(buf));
	if (ret < 0)
		return (int) ret;

	value = strtoll(buf, &end, 0);
	if (end == buf)
		return -EINVAL;
	*val = value;
	return 0;
}

int iio_device_debug_attr_read_bool(const struct iio_device *dev,
		const char *attr, bool *val)
{
	long long value;
	int ret = iio_device_debug_attr_read_longlong(dev, attr, &value);
	if (ret < 0)
		return ret;

	*val = !!value;
	return 0;
}

int iio_device_debug_attr_read_double(const struct iio_device *dev,
		const char *attr, double *val)
{
	char buf[1024];
	ssize_t ret = iio_device_debug_attr_read(dev, attr, buf, sizeof(buf));
	if (ret < 0)
		return (int) ret;
	else
		return read_double(buf, val);
}

int iio_device_debug_attr_write_longlong(const struct iio_device *dev,
		const char *attr, long long val)
{
	ssize_t ret;
	char buf[1024];

	iio_snprintf(buf, sizeof(buf), "%lld", val);
	ret = iio_device_debug_attr_write(dev, attr, buf);

	return ret < 0 ? ret : 0;
}

int iio_device_debug_attr_write_double(const struct iio_device *dev,
		const char *attr, double val)
{
	ssize_t ret;
	char buf[1024];

	ret = (ssize_t) write_double(buf, sizeof(buf), val);
	if (!ret)
		ret = iio_device_debug_attr_write(dev, attr, buf);
	return ret < 0 ? ret : 0;
}

int iio_device_debug_attr_write_bool(const struct iio_device *dev,
		const char *attr, bool val)
{
	ssize_t ret;

	if (val)
		ret = iio_device_debug_attr_write_raw(dev, attr, "1", 2);
	else
		ret = iio_device_debug_attr_write_raw(dev, attr, "0", 2);

	return ret < 0 ? ret : 0;
}

int iio_device_identify_filename(const struct iio_device *dev,
		const char *filename, struct iio_channel **chn,
		const char **attr)
{
	unsigned int i;

	for (i = 0; i < dev->nb_channels; i++) {
		struct iio_channel *ch = dev->channels[i];
		unsigned int j;

		for (j = 0; j < ch->nb_attrs; j++) {
			if (!strcmp(ch->attrs[j].filename, filename)) {
				*attr = ch->attrs[j].name;
				*chn = ch;
				return 0;
			}
		}
	}

	for (i = 0; i < dev->nb_attrs; i++) {
		/* Devices attributes are named after their filename */
		if (!strcmp(dev->attrs[i], filename)) {
			*attr = dev->attrs[i];
			*chn = NULL;
			return 0;
		}
	}

	for (i = 0; i < dev->nb_debug_attrs; i++) {
		if (!strcmp(dev->debug_attrs[i], filename)) {
			*attr = dev->debug_attrs[i];
			*chn = NULL;
			return 0;
		}
	}

	return -EINVAL;
}

int iio_device_reg_write(struct iio_device *dev,
		uint32_t address, uint32_t value)
{
	ssize_t ret;
	char buf[1024];

	iio_snprintf(buf, sizeof(buf), "0x%" PRIx32 " 0x%" PRIx32,
			address, value);
	ret = iio_device_debug_attr_write(dev, "direct_reg_access", buf);

	return ret < 0 ? ret : 0;
}

int iio_device_reg_read(struct iio_device *dev,
		uint32_t address, uint32_t *value)
{
	/* NOTE: There is a race condition here. But it is extremely unlikely to
	 * happen, and as this is a debug function, it shouldn't be used for
	 * something else than debug. */

	long long val;
	int ret = iio_device_debug_attr_write_longlong(dev,
			"direct_reg_access", (long long) address);
	if (ret < 0)
		return ret;

	ret = iio_device_debug_attr_read_longlong(dev,
			"direct_reg_access", &val);
	if (!ret)
		*value = (uint32_t) val;
	return ret;
}

static int read_each_attr(struct iio_device *dev, bool is_debug,
		int (*cb)(struct iio_device *dev,
			const char *attr, const char *val, size_t len, void *d),
		void *data)
{
	int ret;
	char *buf, *ptr;
	unsigned int i, count;

	/* We need a big buffer here; 1 MiB should be enough */
	buf = malloc(0x100000);
	if (!buf)
		return -ENOMEM;

	if (is_debug) {
		count = iio_device_get_debug_attrs_count(dev);
		ret = (int) iio_device_debug_attr_read(dev,
				NULL, buf, 0x100000);
	} else {
		count = iio_device_get_attrs_count(dev);
		ret = (int) iio_device_attr_read(dev, NULL, buf, 0x100000);
	}

	if (ret < 0)
		goto err_free_buf;

	ptr = buf;

	for (i = 0; i < count; i++) {
		const char *attr;
		int32_t len = (int32_t) iio_be32toh(*(uint32_t *) ptr);

		if (is_debug)
			attr = iio_device_get_debug_attr(dev, i);
		else
			attr = iio_device_get_attr(dev, i);

		ptr += 4;
		if (len > 0) {
			ret = cb(dev, attr, ptr, (size_t) len, data);
			if (ret < 0)
				goto err_free_buf;

			if (len & 0x3)
				len = ((len >> 2) + 1) << 2;
			ptr += len;
		}
	}

err_free_buf:
	free(buf);
	return ret < 0 ? ret : 0;
}

static int write_each_attr(struct iio_device *dev, bool is_debug,
		ssize_t (*cb)(struct iio_device *dev,
			const char *attr, void *buf, size_t len, void *d),
		void *data)
{
	char *buf, *ptr;
	unsigned int i, count;
	size_t len = 0x100000;
	int ret;

	/* We need a big buffer here; 1 MiB should be enough */
	buf = malloc(len);
	if (!buf)
		return -ENOMEM;

	ptr = buf;

	if (is_debug)
		count = iio_device_get_debug_attrs_count(dev);
	else
		count = iio_device_get_attrs_count(dev);

	for (i = 0; i < count; i++) {
		const char *attr;

		if (is_debug)
			attr = iio_device_get_debug_attr(dev, i);
		else
			attr = iio_device_get_attr(dev, i);

		ret = (int) cb(dev, attr, ptr + 4, len - 4, data);
		if (ret < 0)
			goto err_free_buf;

		*(int32_t *) ptr = (int32_t) iio_htobe32((uint32_t) ret);
		ptr += 4;
		len -= 4;

		if (ret > 0) {
			if (ret & 0x3)
				ret = ((ret >> 2) + 1) << 2;
			ptr += ret;
			len -= ret;
		}
	}

	if (is_debug)
		ret = (int) iio_device_debug_attr_write_raw(dev,
				NULL, buf, ptr - buf);
	else
		ret = (int) iio_device_attr_write_raw(dev,
				NULL, buf, ptr - buf);

err_free_buf:
	free(buf);
	return ret < 0 ? ret : 0;
}

int iio_device_debug_attr_read_all(struct iio_device *dev,
		int (*cb)(struct iio_device *dev,
			const char *attr, const char *val, size_t len, void *d),
		void *data)
{
	return read_each_attr(dev, true, cb, data);
}

int iio_device_attr_read_all(struct iio_device *dev,
		int (*cb)(struct iio_device *dev,
			const char *attr, const char *val, size_t len, void *d),
		void *data)
{
	return read_each_attr(dev, false, cb, data);
}

int iio_device_debug_attr_write_all(struct iio_device *dev,
		ssize_t (*cb)(struct iio_device *dev,
			const char *attr, void *buf, size_t len, void *d),
		void *data)
{
	return write_each_attr(dev, true, cb, data);
}

int iio_device_attr_write_all(struct iio_device *dev,
		ssize_t (*cb)(struct iio_device *dev,
			const char *attr, void *buf, size_t len, void *d),
		void *data)
{
	return write_each_attr(dev, false, cb, data);
}

const struct iio_context * iio_device_get_context(const struct iio_device *dev)
{
	return dev->ctx;
}
