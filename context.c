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
#include <string.h>

static const char xml_header[] = "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
"<!DOCTYPE context ["
"<!ELEMENT context (device)*>"
"<!ELEMENT device (channel | attribute)*>"
"<!ELEMENT channel (attribute)*>"
"<!ELEMENT attribute EMPTY>"
"<!ATTLIST context name CDATA #REQUIRED>"
"<!ATTLIST device id CDATA #REQUIRED name CDATA #IMPLIED>"
"<!ATTLIST channel id CDATA #REQUIRED type CDATA #REQUIRED name CDATA #IMPLIED>"
"<!ATTLIST attribute name CDATA #REQUIRED>"
"]>";

/* Returns a string containing the XML representation of this context */
static char * context_create_xml(const struct iio_context *ctx)
{
	size_t len = strlen(ctx->name) +
		sizeof(xml_header) - 1 +
		sizeof("<context name=\"\" ></context>");
	size_t *devices_len;
	char *str, *ptr, **devices;
	unsigned int i;

	devices_len = malloc(ctx->nb_devices * sizeof(*devices_len));
	if (!devices_len)
		return NULL;

	devices = malloc(ctx->nb_devices * sizeof(*devices));
	if (!devices)
		goto err_free_devices_len;

	for (i = 0; i < ctx->nb_devices; i++) {
		char *xml = iio_device_get_xml(ctx->devices[i],
				&devices_len[i]);
		if (!xml)
			goto err_free_devices;
		devices[i] = xml;
		len += devices_len[i];
	}

	str = malloc(len);
	if (!str)
		goto err_free_devices;

	sprintf(str, "%s<context name=\"%s\" >", xml_header, ctx->name);
	ptr = strrchr(str, '\0');

	for (i = 0; i < ctx->nb_devices; i++) {
		strcpy(ptr, devices[i]);
		ptr += devices_len[i];
		free(devices[i]);
	}

	free(devices);
	free(devices_len);
	strcpy(ptr, "</context>");
	return str;

err_free_devices:
	while (i--)
		free(devices[i]);
	free(devices);
err_free_devices_len:
	free(devices_len);
	return NULL;
}

const char * iio_context_get_xml(const struct iio_context *ctx)
{
	return ctx->xml;
}

const char * iio_context_get_name(const struct iio_context *ctx)
{
	return ctx->name;
}

void iio_context_destroy(struct iio_context *ctx)
{
	unsigned int i;
	if (ctx->ops->shutdown)
		ctx->ops->shutdown(ctx);

	for (i = 0; i < ctx->nb_devices; i++)
		free_device(ctx->devices[i]);
	if (ctx->nb_devices)
		free(ctx->devices);
	if (ctx->xml)
		free(ctx->xml);
	free(ctx);
}

unsigned int iio_context_get_devices_count(const struct iio_context *ctx)
{
	return ctx->nb_devices;
}

struct iio_device * iio_context_get_device(const struct iio_context *ctx,
		unsigned int index)
{
	if (index >= ctx->nb_devices)
		return NULL;
	else
		return ctx->devices[index];
}

struct iio_device * iio_context_find_device(const struct iio_context *ctx,
		const char *name)
{
	unsigned int i;
	for (i = 0; i < ctx->nb_devices; i++) {
		struct iio_device *dev = ctx->devices[i];
		if (!strcmp(dev->id, name) ||
				(dev->name && !strcmp(dev->name, name)))
			return dev;
	}
	return NULL;
}

static void init_index(struct iio_channel *chn)
{
	char buf[1024];
	long ret = (long) iio_channel_attr_read(chn, "index", buf, sizeof(buf));
	if (ret < 0)
		chn->index = ret;
	else
		chn->index = strtol(buf, NULL, 0);
}

static void init_data_format(struct iio_channel *chn)
{
	char buf[1024];
	int ret = iio_channel_attr_read(chn, "type", buf, sizeof(buf));
	if (ret < 0) {
		chn->format.length = 0;
	} else {
		char endian, sign;
		sscanf(buf, "%ce:%c%u/%u>>%u", &endian, &sign,
				&chn->format.bits,
				&chn->format.length,
				&chn->format.shift);
		chn->format.is_signed = sign == 's';
		chn->format.is_be = endian == 'b';
	}

	ret = iio_channel_attr_read(chn, "scale", buf, sizeof(buf));
	if (ret < 0) {
		chn->format.with_scale = false;
	} else {
		chn->format.with_scale = true;
		chn->format.scale = strtod(buf, NULL);
	}
}

static void reorder_channels(struct iio_device *dev)
{
	bool found;
	unsigned int i;

	/* Reorder channels by index */
	do {
		found = false;
		for (i = 1; i < dev->nb_channels; i++) {
			struct iio_channel **channels = dev->channels;
			long ch1 = channels[i - 1]->index;
			long ch2 = channels[i]->index;

			if (ch2 >= 0 && ((ch1 > ch2) || ch1 < 0)) {
				struct iio_channel *bak = channels[i];
				channels[i] = channels[i - 1];
				channels[i - 1] = bak;
				found = true;
			}
		}
	} while (found);
}

int iio_context_init(struct iio_context *ctx)
{
	unsigned int i;
	for (i = 0; i < ctx->nb_devices; i++) {
		unsigned int j;
		struct iio_device *dev = ctx->devices[i];
		for (j = 0; j < dev->nb_channels; j++) {
			init_index(dev->channels[j]);
			init_data_format(dev->channels[j]);
		}

		reorder_channels(dev);
	}

	ctx->xml = context_create_xml(ctx);
	return ctx->xml ? 0 : -ENOMEM;
}
