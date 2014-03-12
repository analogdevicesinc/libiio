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

/* Returns a string containing the XML representation of this channel */
char * iio_channel_get_xml(const struct iio_channel *chn, size_t *length)
{
	size_t len = sizeof("<channel id=\"\" name=\"\" "
			"type=\"output\" ></channel>");
	char *ptr, *str, *attrs[chn->nb_attrs];
	size_t attrs_len[chn->nb_attrs];
	unsigned int i;

	for (i = 0; i < chn->nb_attrs; i++) {
		char *xml = get_attr_xml(chn->attrs[i], &attrs_len[i]);
		if (!xml)
			goto err_free_attrs;
		attrs[i] = xml;
		len += attrs_len[i];
	}

	len += strlen(chn->id);
	if (chn->name)
		len += strlen(chn->name);

	str = malloc(len);
	if (!str)
		goto err_free_attrs;

	sprintf(str, "<channel id=\"%s\"", chn->id);
	ptr = strrchr(str, '\0');

	if (chn->name) {
		sprintf(ptr, " name=\"%s\"", chn->name);
		ptr = strrchr(ptr, '\0');
	}

	sprintf(ptr, " type=\"%s\" >", chn->is_output ? "output" : "input");
	ptr = strrchr(ptr, '\0');

	for (i = 0; i < chn->nb_attrs; i++) {
		strcpy(ptr, attrs[i]);
		ptr += attrs_len[i];
		free(attrs[i]);
	}

	strcpy(ptr, "</channel>");
	*length = ptr - str + sizeof("</channel>") - 1;
	return str;

err_free_attrs:
	while (i--)
		free(attrs[i]);
	return NULL;
}

const char * iio_channel_get_id(const struct iio_channel *chn)
{
	return chn->id;
}

const char * iio_channel_get_name(const struct iio_channel *chn)
{
	return chn->name;
}

bool iio_channel_is_output(const struct iio_channel *chn)
{
	return chn->is_output;
}

unsigned int iio_channel_get_attrs_count(const struct iio_channel *chn)
{
	return chn->nb_attrs;
}

const char * iio_channel_get_attr(const struct iio_channel *chn,
		unsigned int index)
{
	if (index >= chn->nb_attrs)
		return NULL;
	else
		return chn->attrs[index];
}

ssize_t iio_channel_attr_read(const struct iio_channel *chn,
		const char *attr, char *dst, size_t len)
{
	if (chn->dev->ctx->ops->read_channel_attr)
		return chn->dev->ctx->ops->read_channel_attr(chn,
				attr, dst, len);
	else
		return -ENOSYS;
}

ssize_t iio_channel_attr_write(const struct iio_channel *chn,
		const char *attr, const char *src)
{
	if (chn->dev->ctx->ops->write_channel_attr)
		return chn->dev->ctx->ops->write_channel_attr(chn, attr, src);
	else
		return -ENOSYS;
}

void iio_channel_set_data(struct iio_channel *chn, void *data)
{
	chn->userdata = data;
}

void * iio_channel_get_data(const struct iio_channel *chn)
{
	return chn->userdata;
}

long iio_channel_get_index(const struct iio_channel *chn)
{
	return chn->index;
}

void free_channel(struct iio_channel *chn)
{
	unsigned int i;
	for (i = 0; i < chn->nb_attrs; i++)
		free((char *) chn->attrs[i]);
	if (chn->nb_attrs)
		free(chn->attrs);
	if (chn->name)
		free((char *) chn->name);
	if (chn->id)
		free((char *) chn->id);
	free(chn);
}
