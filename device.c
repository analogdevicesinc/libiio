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

#include <errno.h>
#include <stdio.h>
#include <string.h>

static char *get_attr_xml(const char *attr, size_t *length)
{
	size_t len = sizeof("<attribute name=\"\" />") + strlen(attr);
	char *str = malloc(len);
	if (!str) {
		ERROR("Unable to allocate memory\n");
		return NULL;
	}

	*length = len - 1; /* Skip the \0 */
	sprintf(str, "<attribute name=\"%s\" />", attr);
	return str;
}

/* Returns a string containing the XML representation of this device */
char * iio_device_get_xml(const struct iio_device *dev, size_t *length)
{
	size_t len = sizeof("<device id=\"\" name=\"\" ></device>");
	char *ptr, *str, *attrs[dev->nb_attrs], *channels[dev->nb_channels];
	size_t attrs_len[dev->nb_attrs], channels_len[dev->nb_channels];
	unsigned int i, j;

	for (i = 0; i < dev->nb_attrs; i++) {
		char *xml = get_attr_xml(dev->attrs[i], &attrs_len[i]);
		if (!xml)
			goto err_free_attrs;
		attrs[i] = xml;
		len += attrs_len[i];
	}

	for (j = 0; j < dev->nb_channels; j++) {
		char *xml = iio_channel_get_xml(dev->channels[j],
				&channels_len[j]);
		if (!xml)
			goto err_free_channels;
		channels[j] = xml;
		len += channels_len[j];
	}

	len += strlen(dev->id);
	if (dev->name)
		len += strlen(dev->name);

	str = malloc(len);
	if (!str)
		goto err_free_channels;

	sprintf(str, "<device id=\"%s\"", dev->id);
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

	for (i = 0; i < dev->nb_attrs; i++) {
		strcpy(ptr, attrs[i]);
		ptr += attrs_len[i];
		free(attrs[i]);
	}

	strcpy(ptr, "</device>");
	*length = ptr - str + sizeof("</device>") - 1;
	return str;

err_free_channels:
	while (j--)
		free(channels[j]);
err_free_attrs:
	while (i--)
		free(attrs[i]);
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

static int iio_device_open_mask(const struct iio_device *dev,
		size_t samples_count, uint32_t *mask, size_t words)
{
	unsigned int i;
	bool has_channels = false;

	for (i = 0; !has_channels && i < words; i++)
		has_channels = !!mask[i];
	if (!has_channels)
		return -EINVAL;

	if (dev->ctx->ops->open)
		return dev->ctx->ops->open(dev, samples_count, mask, words);
	else
		return -ENOSYS;
}

int iio_device_open(const struct iio_device *dev, size_t samples_count)
{
	size_t nb = (dev->nb_channels + 31) / 32;
	uint32_t *mask = NULL;
	unsigned int i;
	int ret;

	if (nb == 0)
		return -EINVAL;

	mask = calloc(nb, sizeof(*mask));
	if (!mask)
		return -ENOMEM;

	for (i = 0; i < dev->nb_channels; i++) {
		struct iio_channel *chn = dev->channels[i];
		if (iio_channel_is_enabled(chn) && chn->index >= 0)
			SET_BIT(mask, chn->index);
	}

	ret = iio_device_open_mask(dev, samples_count, mask, nb);
	free(mask);
	return ret;
}

int iio_device_close(const struct iio_device *dev)
{
	if (dev->ctx->ops->close)
		return dev->ctx->ops->close(dev);
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
		return dev->ctx->ops->read_device_attr(dev, attr, dst, len);
	else
		return -ENOSYS;
}

ssize_t iio_device_attr_write(const struct iio_device *dev,
		const char *attr, const char *src)
{
	if (dev->ctx->ops->write_device_attr)
		return dev->ctx->ops->write_device_attr(dev, attr, src);
	else
		return -ENOSYS;
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

int iio_trigger_get_rate(const struct iio_device *trigger, unsigned long *rate)
{
	char buf[1024], *end;
	ssize_t ret;
	unsigned long value;

	if (!iio_device_is_trigger(trigger))
		return -EINVAL;

	ret = iio_device_attr_read(trigger, "frequency", buf, sizeof(buf));
	if (ret < 0)
		return (int) ret;

	value = strtoul(buf, &end, 10);
	if (buf == end)
		return -EBADF;

	*rate = value;
	return 0;
}

int iio_trigger_set_rate(const struct iio_device *trigger, unsigned long rate)
{
	char buf[1024];
	ssize_t ret;

	if (!iio_device_is_trigger(trigger))
		return -EINVAL;

	sprintf(buf, "%lu", rate);
	ret = iio_device_attr_write(trigger, "frequency", buf);
	if (ret < 0)
		return (int) ret;
	else
		return 0;
}

void free_device(struct iio_device *dev)
{
	unsigned int i;
	for (i = 0; i < dev->nb_attrs; i++)
		free((char *) dev->attrs[i]);
	if (dev->nb_attrs)
		free(dev->attrs);
	for (i = 0; i < dev->nb_channels; i++)
		free_channel(dev->channels[i]);
	if (dev->nb_channels)
		free(dev->channels);
	if (dev->mask)
		free(dev->mask);
	if (dev->name)
		free((char *) dev->name);
	if (dev->id)
		free((char *) dev->id);
	free(dev);
}

ssize_t iio_device_get_sample_size_mask(const struct iio_device *dev,
		uint32_t *mask, size_t words)
{
	ssize_t size = 0;
	unsigned int i;

	if (words != (dev->nb_channels + 31) / 32)
		return -EINVAL;

	for (i = 0; i < dev->nb_channels; i++) {
		const struct iio_channel *chn = dev->channels[i];
		unsigned int length = chn->format.length / 8;

		if (chn->index < 0)
			break;
		if (!TEST_BIT(mask, chn->index))
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

ssize_t iio_device_process_samples(const struct iio_device *dev,
		uint32_t *mask, size_t words, void *buf, size_t len,
		ssize_t (*cb)(const struct iio_channel *, void *, void *),
		void *data)
{
	const void *end = buf + len;
	unsigned int i;
	ssize_t processed = 0,
		sample_size = iio_device_get_sample_size_mask(dev, mask, words);
	if (sample_size <= 0)
		return -EINVAL;

	while (end - buf >= sample_size) {
		for (i = 0; i < dev->nb_channels; i++) {
			const struct iio_channel *chn = dev->channels[i];
			unsigned int length = chn->format.length / 8;
			ssize_t ret;

			if (chn->index < 0) {
				ERROR("Channel %s has negative index\n",
						chn->id);
				break;
			}

			if (!TEST_BIT(mask, chn->index))
				continue;

			if ((uintptr_t) buf % length)
				buf += length - ((uintptr_t) buf % length);

			ret = cb(chn, buf, data);
			if (ret < 0)
				return ret;
			else
				processed += ret;
			buf += length;
		}
	}
	return processed;
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
	char *end, buf[1024];
	double value;
	ssize_t ret = iio_device_attr_read(dev, attr, buf, sizeof(buf));
	if (ret < 0)
		return (int) ret;

	value = strtod(buf, &end);
	if (end == buf)
		return -EINVAL;
	*val = value;
	return 0;
}

int iio_device_attr_write_longlong(const struct iio_device *dev,
		const char *attr, long long val)
{
	char buf[1024];
	snprintf(buf, sizeof(buf), "%lld", val);
	return iio_device_attr_write(dev, attr, buf);
}

int iio_device_attr_write_double(const struct iio_device *dev,
		const char *attr, double val)
{
	char buf[1024];
	snprintf(buf, sizeof(buf), "%lf", val);
	return iio_device_attr_write(dev, attr, buf);
}

int iio_device_attr_write_bool(const struct iio_device *dev,
		const char *attr, bool val)
{
	if (val)
		return iio_device_attr_write(dev, attr, "1");
	else
		return iio_device_attr_write(dev, attr, "0");
}
