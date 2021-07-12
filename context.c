// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

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
"<!ATTLIST context name CDATA #REQUIRED version-major CDATA #REQUIRED "
"version-minor CDATA #REQUIRED version-git CDATA #REQUIRED description CDATA #IMPLIED>"
"<!ATTLIST context-attribute name CDATA #REQUIRED value CDATA #REQUIRED>"
"<!ATTLIST device id CDATA #REQUIRED name CDATA #IMPLIED label CDATA #IMPLIED>"
"<!ATTLIST channel id CDATA #REQUIRED type (input|output) #REQUIRED name CDATA #IMPLIED>"
"<!ATTLIST scan-element index CDATA #REQUIRED format CDATA #REQUIRED scale CDATA #IMPLIED>"
"<!ATTLIST attribute name CDATA #REQUIRED filename CDATA #IMPLIED>"
"<!ATTLIST debug-attribute name CDATA #REQUIRED>"
"<!ATTLIST buffer-attribute name CDATA #REQUIRED>"
"]>";

static ssize_t sanitize_xml(char *ptr, ssize_t len, const char *str)
{
	ssize_t count = 0;
	ssize_t ret;

	for (; *str; str++) {
		switch(*str) {
		case '&':
			ret = iio_snprintf(ptr, len, "%s", "&amp;");
			break;
		case '<':
			ret = iio_snprintf(ptr, len, "%s", "&lt;");
			break;
		case '>':
			ret = iio_snprintf(ptr, len, "%s", "&gt;");
			break;
		case '\'':
			ret = iio_snprintf(ptr, len, "%s", "&apos;");
			break;
		case '"':
			ret = iio_snprintf(ptr, len, "%s", "&quot;");
			break;
		default:
			ret = iio_snprintf(ptr, len, "%c", *str);
			break;
		}

		if (ret < 0)
			return ret;

		iio_update_xml_indexes(ret, &ptr, &len, &count);
	}

	return count;
}

ssize_t iio_xml_print_and_sanitized_param(char *ptr, ssize_t len,
					  const char *before, char *param,
					  const char *after)
{
	ssize_t ret, alen = 0;

	/* Print before */
	ret = iio_snprintf(ptr, len, "%s", before);
	if (ret < 0)
		return ret;
	iio_update_xml_indexes(ret, &ptr, &len, &alen);

	/* Print param */
	ret = sanitize_xml(ptr, len, param);
	if (ret < 0)
		return ret;
	iio_update_xml_indexes(ret, &ptr, &len, &alen);

	/* Print after */
	ret = iio_snprintf(ptr, len, "%s", after);
	if (ret < 0)
		return ret;

	return alen + ret;
}

static ssize_t iio_snprintf_context_xml(char *ptr, ssize_t len,
					const struct iio_context *ctx)
{
	ssize_t ret, alen = 0;
	unsigned int i, major, minor;
	char git_tag[64];

	ret = iio_context_get_version(ctx, &major, &minor, git_tag);
	if (ret < 0)
		return ret;

	ret = iio_snprintf(ptr, len,
			   "%s<context name=\"%s\" version-major=\"%u\" "
			   "version-minor=\"%u\" version-git=\"%s\" ",
			   xml_header, ctx->name, major, minor, git_tag);
	if (ret < 0)
		return ret;

	iio_update_xml_indexes(ret, &ptr, &len, &alen);

	if (ctx->description) {
		ret = iio_xml_print_and_sanitized_param(ptr, len,
							"description=\"",
							ctx->description,
							"\" >");
	} else {
		ret = iio_snprintf(ptr, len, ">");
	}
	if (ret < 0)
		return ret;

	iio_update_xml_indexes(ret, &ptr, &len, &alen);

	for (i = 0; i < ctx->nb_attrs; i++) {
		ret = iio_snprintf(ptr, len,
				   "<context-attribute name=\"%s\" ",
				   ctx->attrs[i]);
		if (ret < 0)
			return ret;

		iio_update_xml_indexes(ret, &ptr, &len, &alen);

		ret = iio_xml_print_and_sanitized_param(ptr, len,
							"value=\"",
							ctx->values[i],
							"\" />");
		if (ret < 0)
			return ret;

		iio_update_xml_indexes(ret, &ptr, &len, &alen);
	}

	for (i = 0; i < ctx->nb_devices; i++) {
		ret = iio_snprintf_device_xml(ptr, len, ctx->devices[i]);
		if (ret < 0)
			return ret;

		iio_update_xml_indexes(ret, &ptr, &len, &alen);
	}

	ret = iio_snprintf(ptr, len, "</context>");
	if (ret < 0)
		return ret;

	return alen + ret;
}

/* Returns a string containing the XML representation of this context */
static char * iio_context_create_xml(const struct iio_context *ctx)
{
	ssize_t len;
	char *str;

	len = iio_snprintf_context_xml(NULL, 0, ctx);
	if (len < 0)
		return ERR_PTR((int) len);

	len++; /* room for terminating NULL */
	str = malloc(len);
	if (!str)
		return ERR_PTR(-ENOMEM);

	len = iio_snprintf_context_xml(str, len, ctx);
	if (len < 0) {
		free(str);
		return ERR_PTR((int) len);
	}

	return str;
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
	free(ctx->attrs);
	free(ctx->values);
	for (i = 0; i < ctx->nb_devices; i++)
		free_device(ctx->devices[i]);
	free(ctx->devices);
	free(ctx->xml);
	free(ctx->description);
	free(ctx->git_tag);
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
		    (dev->label && !strcmp(dev->label, name)) ||
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
		if (IS_ERR(ctx->xml))
			return PTR_ERR(ctx->xml);
	}

	return 0;
}

int iio_context_get_version(const struct iio_context *ctx,
		unsigned int *major, unsigned int *minor, char git_tag[8])
{
	if (ctx->git_tag) {
		if (major)
			*major = ctx->major;
		if (minor)
			*minor = ctx->minor;
		if (git_tag)
			iio_strlcpy(git_tag, ctx->git_tag, 8);

		return 0;
	}

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
	if (WITH_LOCAL_BACKEND && strcmp(uri, "local:") == 0) /* No address part */
		return iio_create_local_context();

	if (WITH_XML_BACKEND && strncmp(uri, "xml:", sizeof("xml:") - 1) == 0)
		return iio_create_xml_context(uri + sizeof("xml:") - 1);

	if (WITH_NETWORK_BACKEND && strncmp(uri, "ip:", sizeof("ip:") - 1) == 0)
		return iio_create_network_context(uri+3);

	if (WITH_USB_BACKEND && strncmp(uri, "usb:", sizeof("usb:") - 1) == 0)
		return usb_create_context_from_uri(uri);

	if (WITH_SERIAL_BACKEND && strncmp(uri, "serial:", sizeof("serial:") - 1) == 0)
		return serial_create_context_from_uri(uri);

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
	if (WITH_LOCAL_BACKEND)
		return local_create_context();

	errno = ENOSYS;
	return NULL;
}

struct iio_context * iio_create_network_context(const char *hostname)
{
	if (WITH_NETWORK_BACKEND)
		return network_create_context(hostname);

	errno = ENOSYS;
	return NULL;
}

struct iio_context * iio_create_xml_context_mem(const char *xml, size_t len)
{
	if (WITH_XML_BACKEND)
		return xml_create_context_mem(xml, len);

	errno = ENOSYS;
	return NULL;
}

struct iio_context * iio_create_xml_context(const char *xml_file)
{
	if (WITH_XML_BACKEND)
		return xml_create_context(xml_file);

	errno = ENOSYS;
	return NULL;
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
