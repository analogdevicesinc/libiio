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

int iio_device_open(const struct iio_device *dev)
{
	if (dev->ctx->ops->open)
		return dev->ctx->ops->open(dev);
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

ssize_t iio_device_read_raw(const struct iio_device *dev,
		void *dst, size_t len)
{
	if (dev->ctx->ops->read)
		return dev->ctx->ops->read(dev, dst, len);
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
	if (dev->name)
		free((char *) dev->name);
	if (dev->id)
		free((char *) dev->id);
	free(dev);
}
