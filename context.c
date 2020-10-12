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
#include "iio-config.h"
#include "iio-private.h"
#include "sort.h"

#include <errno.h>
#include <string.h>

static const char xml_header[] = "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
"<!DOCTYPE context ["
"<!ELEMENT context (device | context-attribute)*>"
"<!ELEMENT context-attribute EMPTY>"
"<!ELEMENT device (channel | attribute | debug-attribute | buffer-attribute)*>"
"<!ELEMENT channel (scan-element?, attribute*)>"
"<!ELEMENT attribute EMPTY>"
"<!ELEMENT scan-element EMPTY>"
"<!ELEMENT debug-attribute EMPTY>"
"<!ELEMENT buffer-attribute EMPTY>"
"<!ATTLIST context name CDATA #REQUIRED description CDATA #IMPLIED>"
"<!ATTLIST context-attribute name CDATA #REQUIRED value CDATA #REQUIRED>"
"<!ATTLIST device id CDATA #REQUIRED name CDATA #IMPLIED>"
"<!ATTLIST channel id CDATA #REQUIRED type (input|output) #REQUIRED name CDATA #IMPLIED>"
"<!ATTLIST scan-element index CDATA #REQUIRED format CDATA #REQUIRED scale CDATA #IMPLIED>"
"<!ATTLIST attribute name CDATA #REQUIRED filename CDATA #IMPLIED>"
"<!ATTLIST debug-attribute name CDATA #REQUIRED>"
"<!ATTLIST buffer-attribute name CDATA #REQUIRED>"
"]>";

/* Returns a string containing the XML representation of this context */
char * iio_context_create_xml(const struct iio_context *ctx)
{
	ssize_t len;
	size_t *devices_len = NULL;
	char *str, *ptr, *eptr, **devices = NULL;
	char ** ctx_attrs, **ctx_values;
	unsigned int i;

	len = sizeof(xml_header) - 1;
	len += strnlen(ctx->name, MAX_CTX_NAME);
	len += sizeof("<context name=\"\" ></context>") - 1;

	if (ctx->description) {
		len += strnlen(ctx->description, MAX_CTX_DESC);
		len += sizeof(" description=\"\"") - 1;
	}

	ctx_attrs = calloc(ctx->nb_attrs, sizeof(*ctx->attrs));
	if (!ctx_attrs) {
		errno = ENOMEM;
		return NULL;
	}
	ctx_values = calloc(ctx->nb_attrs, sizeof(*ctx->values));
	if (!ctx_values) {
		errno = ENOMEM;
		goto err_free_ctx_attrs;
	}

	for (i = 0; i < ctx->nb_attrs; i++) {
		ctx_attrs[i] = encode_xml_ndup(ctx->attrs[i]);
		ctx_values[i] = encode_xml_ndup(ctx->values[i]);
		if (!ctx_attrs[i] || !ctx_values[i])
			goto err_free_ctx_attrs_values;

		len += strnlen(ctx_attrs[i], MAX_ATTR_NAME);
		len += strnlen(ctx_values[i], MAX_ATTR_VALUE);
		len += sizeof("<context-attribute name=\"\" value=\"\" />") - 1;
	}

	if (ctx->nb_devices) {
		devices_len = malloc(ctx->nb_devices * sizeof(*devices_len));
		if (!devices_len) {
			errno = ENOMEM;
			goto err_free_ctx_attrs_values;
		}

		devices = calloc(ctx->nb_devices, sizeof(*devices));
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
	}

	len++; /* room for terminating NULL */
	str = malloc(len);
	if (!str) {
		errno = ENOMEM;
		goto err_free_devices;
	}
	eptr = str + len;
	ptr = str;

	if (len > 0) {
		if (ctx->description) {
			ptr += iio_snprintf(str, len, "%s<context name=\"%s\" "
					"description=\"%s\" >",
					xml_header, ctx->name, ctx->description);
		} else {
			ptr += iio_snprintf(str, len, "%s<context name=\"%s\" >",
					xml_header, ctx->name);
		}
		len = eptr - ptr;
	}

	for (i = 0; i < ctx->nb_attrs && len > 0; i++) {
		ptr += iio_snprintf(ptr, len, "<context-attribute name=\"%s\" value=\"%s\" />",
				ctx_attrs[i], ctx_values[i]);
		free(ctx_attrs[i]);
		free(ctx_values[i]);
		len = eptr - ptr;
	}

	free(ctx_attrs);
	free(ctx_values);

	for (i = 0; i < ctx->nb_devices; i++) {
		if (len > (ssize_t) devices_len[i]) {
			memcpy(ptr, devices[i], devices_len[i]); /* Flawfinder: ignore */
			ptr += devices_len[i];
			len -= devices_len[i];
		}
		free(devices[i]);
	}

	free(devices);
	free(devices_len);

	if (len > 0) {
		ptr += iio_strlcpy(ptr, "</context>", len);
		len -= sizeof("</context>") - 1;
	}

	if (len != 1) {
		IIO_ERROR("Internal libIIO error: iio_context_create_xml str length issue\n");
		free(str);
		return NULL;
	}

	return str;

err_free_devices:
	for (i = 0; i < ctx->nb_devices; i++)
		free(devices[i]);
	free(devices);
err_free_devices_len:
	free(devices_len);
err_free_ctx_attrs_values:
	for (i = 0; i < ctx->nb_attrs; i++) {
		if (ctx_attrs[i])
			free(ctx_attrs[i]);
		if (ctx_values[i])
			free(ctx_values[i]);
	}

	free(ctx_values);
err_free_ctx_attrs:
	free(ctx_attrs);
	return NULL;
}

struct iio_context * iio_context_create_from_backend(
		const struct iio_backend *backend,
		const char *description)
{
	struct iio_context *ctx;
	int ret;

	if (!backend) {
		errno = EINVAL;
		return NULL;
	}

	ctx = zalloc(sizeof(*ctx));
	if (!ctx) {
		errno = ENOMEM;
		return NULL;
	}

	ret = -ENOMEM;
	if (backend->sizeof_context_pdata) {
		ctx->pdata = zalloc(backend->sizeof_context_pdata);
		if (!ctx->pdata)
			goto err_free_ctx;
	}

	if (description) {
		ctx->description = iio_strdup(description);
		if (!ctx->description)
			goto err_free_pdata;
	}

	ctx->name = backend->name;
	ctx->ops = backend->ops;

	return ctx;

err_free_pdata:
	if (ctx->pdata)
		free(ctx->pdata);
err_free_ctx:
	free(ctx);
	errno = -ret;
	return NULL;
}

struct iio_context_pdata * iio_context_get_pdata(const struct iio_context *ctx)
{
	return ctx->pdata;
}

const char * iio_context_get_xml(const struct iio_context *ctx)
{
	return ctx->xml;
}

const char * iio_context_get_name(const struct iio_context *ctx)
{
	return ctx->name;
}

const char * iio_context_get_description(const struct iio_context *ctx)
{
	if (ctx->description)
		return ctx->description;
	else
		return "";
}

void iio_context_destroy(struct iio_context *ctx)
{
	unsigned int i;
	if (ctx->ops->shutdown)
		ctx->ops->shutdown(ctx);

	for (i = 0; i < ctx->nb_attrs; i++) {
		free(ctx->attrs[i]);
		free(ctx->values[i]);
	}
	if (ctx->nb_attrs) {
		free(ctx->attrs);
		free(ctx->values);
	}
	for (i = 0; i < ctx->nb_devices; i++)
		free_device(ctx->devices[i]);
	if (ctx->nb_devices)
		free(ctx->devices);
	if (ctx->xml)
		free(ctx->xml);
	if (ctx->description)
		free(ctx->description);
	if (ctx->pdata)
		free(ctx->pdata);
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

			if (ch1 == ch2 && ch1 >= 0) {
				ch1 = channels[i - 1]->format.shift;
				ch2 = channels[i]->format.shift;
			}

			if (ch2 >= 0 && ((ch1 > ch2) || ch1 < 0)) {
				struct iio_channel *bak = channels[i];
				channels[i] = channels[i - 1];
				channels[i - 1] = bak;
				found = true;
			}
		}
	} while (found);

	for (i = 0; i < dev->nb_channels; i++)
		dev->channels[i]->number = i;
}

int iio_context_init(struct iio_context *ctx)
{
	unsigned int i;

	for (i = 0; i < ctx->nb_devices; i++)
		reorder_channels(ctx->devices[i]);

	if (!ctx->xml) {
		ctx->xml = iio_context_create_xml(ctx);
		if (!ctx->xml)
			return -ENOMEM;
	}

	return 0;
}

int iio_context_get_version(const struct iio_context *ctx,
		unsigned int *major, unsigned int *minor, char git_tag[8])
{
	if (ctx->ops->get_version)
		return ctx->ops->get_version(ctx, major, minor, git_tag);

	iio_library_get_version(major, minor, git_tag);
	return 0;
}

int iio_context_set_timeout(struct iio_context *ctx, unsigned int timeout)
{
	if (ctx->ops->set_timeout)
		return ctx->ops->set_timeout(ctx, timeout);
	else
		return -ENOSYS;
}

struct iio_context * iio_context_clone(const struct iio_context *ctx)
{
	if (ctx->ops->clone) {
		return ctx->ops->clone(ctx);
	} else {
		errno = ENOSYS;
		return NULL;
	}
}

struct iio_context * iio_create_context_from_uri(const char *uri)
{
#ifdef WITH_LOCAL_BACKEND
	if (strcmp(uri, "local:") == 0) /* No address part */
		return iio_create_local_context();
#endif

#ifdef WITH_XML_BACKEND
	if (strncmp(uri, "xml:", sizeof("xml:") - 1) == 0)
		return iio_create_xml_context(uri + sizeof("xml:") - 1);
#endif

#ifdef WITH_NETWORK_BACKEND
	if (strncmp(uri, "ip:", sizeof("ip:") - 1) == 0)
		return iio_create_network_context(uri+3);
#endif

#ifdef WITH_USB_BACKEND
	if (strncmp(uri, "usb:", sizeof("usb:") - 1) == 0)
		return usb_create_context_from_uri(uri);
#endif

#ifdef WITH_SERIAL_BACKEND
	if (strncmp(uri, "serial:", sizeof("serial:") - 1) == 0)
		return serial_create_context_from_uri(uri);
#endif

	errno = ENOSYS;
	return NULL;
}

struct iio_context * iio_create_default_context(void)
{
	char *hostname = iio_getenv("IIOD_REMOTE");
	struct iio_context * ctx;

	if (hostname) {
		ctx = iio_create_context_from_uri(hostname);
		free(hostname);
		return ctx;
	}

	return iio_create_local_context();
}

struct iio_context * iio_create_local_context(void)
{
#ifdef WITH_LOCAL_BACKEND
	return local_create_context();
#else
	errno = ENOSYS;
	return NULL;
#endif
}

struct iio_context * iio_create_network_context(const char *hostname)
{
#ifdef WITH_NETWORK_BACKEND
	return network_create_context(hostname);
#else
	errno = ENOSYS;
	return NULL;
#endif
}

struct iio_context * iio_create_xml_context_mem(const char *xml, size_t len)
{
#ifdef WITH_XML_BACKEND
	return xml_create_context_mem(xml, len);
#else
	errno = ENOSYS;
	return NULL;
#endif
}

struct iio_context * iio_create_xml_context(const char *xml_file)
{
#ifdef WITH_XML_BACKEND
	return xml_create_context(xml_file);
#else
	errno = ENOSYS;
	return NULL;
#endif
}

unsigned int iio_context_get_attrs_count(const struct iio_context *ctx)
{
	return ctx->nb_attrs;
}

int iio_context_get_attr(const struct iio_context *ctx, unsigned int index,
		const char **name, const char **value)
{
	if (index >= ctx->nb_attrs)
		return -EINVAL;

	if (name)
		*name = ctx->attrs[index];
	if (value)
		*value = ctx->values[index];
	return 0;
}

const char * iio_context_get_attr_value(
		const struct iio_context *ctx, const char *name)
{
	unsigned int i;

	for (i = 0; i < ctx->nb_attrs; i++) {
		if (!strcmp(name, ctx->attrs[i]))
			return ctx->values[i];
	}

	return NULL;
}

int iio_context_add_device(struct iio_context *ctx, struct iio_device *dev)
{
	struct iio_device **devices = realloc(ctx->devices,
			(ctx->nb_devices + 1) * sizeof(struct iio_device *));

	if (!devices) {
		IIO_ERROR("Unable to allocate memory\n");
		return -ENOMEM;
	}

	devices[ctx->nb_devices++] = dev;
	ctx->devices = devices;
	IIO_DEBUG("Added device \'%s\' to context \'%s\'\n", dev->id, ctx->name);
	return 0;
}

int iio_context_add_attr(struct iio_context *ctx,
		const char *key, const char *value)
{
	char **attrs, **values, *new_key, *new_val;
	unsigned int i;

	for (i = 0; i < ctx->nb_attrs; i++) {
		if(!strcmp(ctx->attrs[i], key)) {
			new_val = iio_strdup(value);
			if (!new_val)
				return -ENOMEM;
			free(ctx->values[i]);
			ctx->values[i] = new_val;
			return 0;
		}
	}

	attrs = realloc(ctx->attrs,
			(ctx->nb_attrs + 1) * sizeof(*ctx->attrs));
	if (!attrs)
		return -ENOMEM;

	ctx->attrs = attrs;

	values = realloc(ctx->values,
			(ctx->nb_attrs + 1) * sizeof(*ctx->values));
	if (!values)
		return -ENOMEM;

	ctx->values = values;

	new_key = iio_strdup(key);
	if (!new_key)
		return -ENOMEM;

	new_val = iio_strdup(value);
	if (!new_val) {
		free(new_key);
		return -ENOMEM;
	}

	ctx->attrs[ctx->nb_attrs] = new_key;
	ctx->values[ctx->nb_attrs] = new_val;
	ctx->nb_attrs++;
	return 0;
}
