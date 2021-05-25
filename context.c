// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "iio-debug.h"
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
"<!ATTLIST device id CDATA #REQUIRED name CDATA #IMPLIED label CDATA #IMPLIED>"
"<!ATTLIST channel id CDATA #REQUIRED type (input|output) #REQUIRED name CDATA #IMPLIED>"
"<!ATTLIST scan-element index CDATA #REQUIRED format CDATA #REQUIRED scale CDATA #IMPLIED>"
"<!ATTLIST attribute name CDATA #REQUIRED filename CDATA #IMPLIED>"
"<!ATTLIST debug-attribute name CDATA #REQUIRED>"
"<!ATTLIST buffer-attribute name CDATA #REQUIRED>"
"]>";

static const struct iio_context_params default_params = {
	.timeout_ms = 0,

	.out = NULL, /* stdout */
	.err = NULL, /* stderr */
	.log_level = (enum iio_log_level)DEFAULT_LOG_LEVEL,
	.stderr_level = LEVEL_WARNING,
};

const struct iio_context_params *get_default_params(void)
{
	return &default_params;
}

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

		if (ptr) {
			len -= ret;
			ptr += ret;
		}

		count += ret;
	}

	return count;
}

static ssize_t iio_snprintf_context_xml(char *ptr, ssize_t len,
					const struct iio_context *ctx)
{
	ssize_t ret, alen = 0;
	unsigned int i;

	if (ctx->description)
		ret = iio_snprintf(ptr, len, "%s<context name=\"%s\" "
				   "description=\"%s\" >",
				   xml_header, ctx->name, ctx->description);
	else
		ret = iio_snprintf(ptr, len, "%s<context name=\"%s\" >",
				   xml_header, ctx->name);

	if (ret < 0)
		return ret;

	if (ptr) {
		ptr += ret;
		len -= ret;
	}
	alen += ret;

	for (i = 0; i < ctx->nb_attrs; i++) {
		ret = iio_snprintf(ptr, len,
				   "<context-attribute name=\"%s\" value=\"",
				   ctx->attrs[i]);
		if (ret < 0)
			return ret;
		if (ptr) {
			ptr += ret;
			len -= ret;
		}
		alen += ret;

		ret = sanitize_xml(ptr, len, ctx->values[i]);
		if (ret < 0)
			return ret;
		if (ptr) {
			ptr += ret;
			len -= ret;
		}
		alen += ret;

		ret = iio_snprintf(ptr, len, "\" />");
		if (ret < 0)
			return ret;
		if (ptr) {
			ptr += ret;
			len -= ret;
		}
		alen += ret;
	}

	for (i = 0; i < ctx->nb_devices; i++) {
		ret = iio_snprintf_device_xml(ptr, len, ctx->devices[i]);
		if (ret < 0)
			return ret;
		if (ptr) {
			ptr += ret;
			len -= ret;
		}
		alen += ret;
	}

	ret = iio_snprintf(ptr, len, "</context>");
	if (ret < 0)
		return ret;
	alen += ret;

	return alen;
}

/* Returns a string containing the XML representation of this context */
static char * iio_context_create_xml(const struct iio_context *ctx)
{
	ssize_t len;
	char *str;

	len = iio_snprintf_context_xml(NULL, 0, ctx);
	if (len < 0)
		return ERR_TO_PTR(len);

	len++; /* room for terminating NULL */
	str = malloc(len);
	if (!str)
		return ERR_TO_PTR(-ENOMEM);

	len = iio_snprintf_context_xml(str, len, ctx);
	if (len < 0) {
		free(str);
		return ERR_TO_PTR(len);
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
	if (description) {
		ctx->description = iio_strdup(description);
		if (!ctx->description)
			goto err_free_ctx;
	}

	ctx->name = backend->name;
	ctx->ops = backend->ops;

	return ctx;

err_free_ctx:
	free(ctx);
	errno = -ret;
	return NULL;
}

const struct iio_context_params *
iio_context_get_params(const struct iio_context *ctx)
{
	return &ctx->params;
}

struct iio_context_pdata * iio_context_get_pdata(const struct iio_context *ctx)
{
	return ctx->pdata;
}

void iio_context_set_pdata(struct iio_context *ctx, struct iio_context_pdata *d)
{
	ctx->pdata = d;
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
	void *lib = ctx->lib;

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
	free(ctx->pdata);
	free(ctx);

	if (WITH_MODULES && lib)
		iio_release_module(lib);
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
			return PTR_TO_ERR(ctx->xml);
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
	return iio_create_context(NULL, uri);
}

static const struct iio_backend *iio_backends[] = {
	IF_ENABLED(WITH_LOCAL_BACKEND, &iio_local_backend),
	IF_ENABLED(WITH_NETWORK_BACKEND, &iio_ip_backend),
	IF_ENABLED(WITH_SERIAL_BACKEND && !WITH_SERIAL_BACKEND_DYNAMIC,
		   &iio_serial_backend),
	IF_ENABLED(WITH_USB_BACKEND, &iio_usb_backend),
	IF_ENABLED(WITH_XML_BACKEND, &iio_xml_backend),
};

struct iio_context * iio_create_context(const struct iio_context_params *params,
					const char *uri)
{
	struct iio_context_params params2 = { 0 };
	const struct iio_backend *backend = NULL;
	struct iio_context *ctx;
	char *uri_dup = NULL;
	unsigned int i;

	if (params)
		params2 = *params;

	if (!params2.log_level)
		params2.log_level = default_params.log_level;
	if (!params2.stderr_level)
		params2.stderr_level = default_params.stderr_level;

	if (!uri) {
		uri_dup = iio_getenv("IIOD_REMOTE");

		uri = uri_dup ? uri_dup : "local:";
	}

	for (i = 0; !backend && i < ARRAY_SIZE(iio_backends); i++) {
		if (!iio_backends[i])
			continue;

		if (!strncmp(uri, iio_backends[i]->uri_prefix,
			     strlen(iio_backends[i]->uri_prefix))) {
			backend = iio_backends[i];
		}
	}

	if (backend) {
		if (!params2.timeout_ms)
			params2.timeout_ms = backend->default_timeout_ms;

		ctx = backend->ops->create(&params2,
					   uri + strlen(backend->uri_prefix));
	} else if (WITH_MODULES) {
		ctx = iio_create_dynamic_context(&params2, uri);
	} else {
		errno = ENOSYS;
		ctx = NULL;
	}

	free(uri_dup);

	return ctx;
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

	return iio_create_context_from_uri("local:");
}

struct iio_context * iio_create_xml_context_mem(const char *xml, size_t len)
{
	if (WITH_XML_BACKEND)
		return xml_create_context_mem(&default_params, xml, len);

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
		ctx_err(ctx, "Unable to allocate memory\n");
		return -ENOMEM;
	}

	devices[ctx->nb_devices++] = dev;
	ctx->devices = devices;

	ctx_dbg(ctx, "Added device \'%s\' to context \'%s\'\n",
		dev->id, ctx->name);
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

struct iio_context *
iio_create_context_from_xml(const struct iio_context_params *params,
			    const char *xml, size_t xml_len,
			    const struct iio_backend *backend,
			    const char *description, const char **ctx_attrs,
			    const char **ctx_values, unsigned int nb_ctx_attrs)
{
	struct iio_context *ctx;
	char *new_description;
	unsigned int i;
	ssize_t len;
	int ret;

	if (!WITH_XML_BACKEND) {
		errno = ENOSYS;
		return NULL;
	}

	ctx = xml_create_context_mem(params, xml, xml_len);
	if (!ctx)
		return NULL;

	if (backend) {
		ctx->name = backend->name;
		ctx->ops = backend->ops;
	}

	for (i = 0; i < nb_ctx_attrs; i++) {
		ret = iio_context_add_attr(ctx, ctx_attrs[i], ctx_values[i]);
		if (ret < 0) {
			prm_perror(params, -ret, "Unable to add context attribute\n");
			goto err_context_destroy;
		}
	}

	if (ctx->description) {
		len = iio_snprintf(NULL, 0, "%s %s",
				   ctx->description, description);
		if (len < 0) {
			prm_perror(params, -len, "Unable to set context description\n");
			goto err_context_destroy;
		}

		new_description = malloc(len + 1);
		if (!new_description) {
			prm_err(params, "Unable to alloc memory\n");
			goto err_context_destroy;
		}

		iio_snprintf(new_description, len + 1, "%s %s",
			     ctx->description, description);
	} else {
		new_description = iio_strdup(description);
		if (!new_description) {
			prm_err(params, "Unable to alloc memory\n");
			goto err_context_destroy;
		}
	}

	free(ctx->description);
	ctx->description = new_description;
	ctx->params = *params;

	return ctx;

err_context_destroy:
	iio_context_destroy(ctx);
	return NULL;
}
