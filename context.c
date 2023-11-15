// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "attr.h"
#include "iio-config.h"
#include "iio-private.h"
#include "sort.h"

#include <iio/iio-debug.h>

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

		iio_update_xml_indexes(ret, &ptr, &len, &count);
	}

	return count;
}

ssize_t iio_xml_print_and_sanitized_param(char *ptr, ssize_t len,
					  const char *before,
					  const char *param,
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
	unsigned int i;

	ret = iio_snprintf(ptr, len,
			   "%s<context name=\"%s\" version-major=\"%u\" "
			   "version-minor=\"%u\" version-git=\"%s\" ",
			   xml_header, ctx->name,
			   iio_context_get_version_major(ctx),
			   iio_context_get_version_minor(ctx),
			   iio_context_get_version_tag(ctx));
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

	for (i = 0; i < ctx->attrlist.num; i++) {
		ret = iio_snprintf(ptr, len,
				   "<context-attribute name=\"%s\" ",
				   ctx->attrlist.attrs[i].name);
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
		return iio_ptr((int) len);

	len++; /* room for terminating NULL */
	str = malloc(len);
	if (!str)
		return iio_ptr(-ENOMEM);

	len = iio_snprintf_context_xml(str, len, ctx);
	if (len < 0) {
		free(str);
		return iio_ptr((int) len);
	}

	return str;
}

struct iio_context * iio_context_create_from_backend(
		const struct iio_backend *backend,
		const char *description)
{
	struct iio_context *ctx;
	int ret;

	if (!backend)
		return iio_ptr(-EINVAL);

	ctx = zalloc(sizeof(*ctx));
	if (!ctx)
		return iio_ptr(-ENOMEM);

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
	return iio_ptr(ret);
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

	for (i = 0; i < ctx->attrlist.num; i++)
		free(ctx->values[i]);
	free(ctx->values);
	iio_free_attrs(&ctx->attrlist);
	for (i = 0; i < ctx->nb_devices; i++)
		free_device(ctx->devices[i]);
	free(ctx->devices);
	free(ctx->xml);
	free(ctx->description);
	free(ctx->git_tag);
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
	unsigned int i;

	/* Reorder channels by index */
	iio_sort_channels(dev);

	for (i = 0; i < dev->nb_channels; i++)
		dev->channels[i]->number = i;
}

int iio_context_init(struct iio_context *ctx)
{
	unsigned int i;

	for (i = 0; i < ctx->nb_devices; i++)
		reorder_channels(ctx->devices[i]);

	if (ctx->xml)
		return 0;

	ctx->xml = iio_context_create_xml(ctx);

	return iio_err(ctx->xml);
}

unsigned int iio_context_get_version_major(const struct iio_context *ctx)
{
	if (ctx && ctx->git_tag)
		return ctx->major;

	return LIBIIO_VERSION_MAJOR;
}

unsigned int iio_context_get_version_minor(const struct iio_context *ctx)
{
	if (ctx && ctx->git_tag)
		return ctx->minor;

	return LIBIIO_VERSION_MINOR;
}

const char * iio_context_get_version_tag(const struct iio_context *ctx)
{
	if (ctx && ctx->git_tag)
		return ctx->git_tag;

	return LIBIIO_VERSION_GIT;
}

int iio_context_set_timeout(struct iio_context *ctx, unsigned int timeout)
{
	int ret = 0;

	if (ctx->ops->set_timeout) {
		ret = ctx->ops->set_timeout(ctx, timeout);
		if (ret)
			return ret;
	}

	ctx->params.timeout_ms = timeout;

	return 0;
}

const struct iio_backend * const iio_backends[] = {
	IF_ENABLED(WITH_LOCAL_BACKEND, &iio_local_backend),
	IF_ENABLED(WITH_NETWORK_BACKEND && !WITH_NETWORK_BACKEND_DYNAMIC,
		   &iio_ip_backend),
	IF_ENABLED(WITH_SERIAL_BACKEND && !WITH_SERIAL_BACKEND_DYNAMIC,
		   &iio_serial_backend),
	IF_ENABLED(WITH_USB_BACKEND && !WITH_USB_BACKEND_DYNAMIC,
		   &iio_usb_backend),
	IF_ENABLED(WITH_XML_BACKEND, &iio_xml_backend),
};
const unsigned int iio_backends_size = ARRAY_SIZE(iio_backends);

static int iio_context_update_scale_offset(struct iio_context *ctx)
{
	const struct iio_attr *attr;
	struct iio_channel *chn;
	struct iio_device *dev;
	unsigned int i, j;
	int err;

	if (!ctx->ops->read_attr)
		return 0;

	for (i = 0; i < ctx->nb_devices; i++) {
		dev = ctx->devices[i];

		for (j = 0; j < dev->nb_channels; j++) {
			chn = dev->channels[j];

			attr = iio_channel_find_attr(chn, "scale");
			if (attr) {
				err = iio_attr_read_double(attr,
							   &chn->format.scale);
				if (err) {
					chn_perror(chn, err, "Unable to read scale");
					return err;
				}

				chn->format.with_scale = true;
			}

			attr = iio_channel_find_attr(chn, "offset");
			if (attr) {
				err = iio_attr_read_double(attr,
							   &chn->format.offset);
				if (err) {
					chn_perror(chn, err, "Unable to read offset");
					return err;
				}
			}
		}
	}

	return 0;
}

struct iio_context * iio_create_context(const struct iio_context_params *params,
					const char *uri)
{
	struct iio_context_params params2 = { 0 };
	const struct iio_backend *backend = NULL;
	struct iio_context *ctx = NULL;
	char *uri_dup = NULL;
	unsigned int i;
	int err;

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
		ctx = iio_ptr(-ENOSYS);
	}

	free(uri_dup);

	if (!iio_err(ctx)) {
		err = iio_context_update_scale_offset(ctx);
		if (err) {
			iio_context_destroy(ctx);
			ctx = iio_ptr(err);
		}
	}

	return ctx;
}

unsigned int iio_context_get_attrs_count(const struct iio_context *ctx)
{
	return ctx->attrlist.num;
}

const struct iio_attr *
iio_context_get_attr(const struct iio_context *ctx, unsigned int index)
{
	return iio_attr_get(&ctx->attrlist, index);
}

const struct iio_attr *
iio_context_find_attr(const struct iio_context *ctx, const char *name)
{
	return iio_attr_find(&ctx->attrlist, name);
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

struct iio_context *
iio_create_context_from_xml(const struct iio_context_params *params,
			    const char *uri, const struct iio_backend *backend,
			    const char *description, const char **ctx_attrs,
			    const char **ctx_values, unsigned int nb_ctx_attrs)
{
	struct iio_context *ctx;
	char *new_description;
	unsigned int i;
	ssize_t len;
	int ret;

	if (!WITH_XML_BACKEND)
		return iio_ptr(-ENOSYS);

	ctx = iio_create_context(params, uri);
	if (iio_err(ctx))
		return ctx;

	if (backend) {
		ctx->name = backend->name;
		ctx->ops = backend->ops;
	}

	for (i = 0; i < nb_ctx_attrs; i++) {
		ret = iio_context_add_attr(ctx, ctx_attrs[i], ctx_values[i]);
		if (ret < 0) {
			prm_perror(params, ret, "Unable to add context attribute");
			goto err_context_destroy;
		}
	}

	if (description && ctx->description) {
		len = iio_snprintf(NULL, 0, "%s %s",
				   ctx->description, description);
		if (len < 0) {
			ret = (int) len;
			prm_perror(params, ret, "Unable to set context description");
			goto err_context_destroy;
		}

		new_description = malloc(len + 1);
		if (!new_description) {
			ret = -ENOMEM;
			prm_err(params, "Unable to alloc memory\n");
			goto err_context_destroy;
		}

		iio_snprintf(new_description, len + 1, "%s %s",
			     ctx->description, description);
	} else if (description) {
		new_description = iio_strdup(description);
		if (!new_description) {
			ret = -ENOMEM;
			prm_err(params, "Unable to alloc memory\n");
			goto err_context_destroy;
		}

		free(ctx->description);
		ctx->description = new_description;
	}

	ctx->params = *params;

	return ctx;

err_context_destroy:
	iio_context_destroy(ctx);
	return iio_ptr(ret);
}

void iio_context_set_data(struct iio_context *ctx, void *data)
{
	ctx->userdata = data;
}

void * iio_context_get_data(const struct iio_context *ctx)
{
	return ctx->userdata;
}
