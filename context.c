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

#ifdef _WIN32
#define LOCAL_BACKEND 0
#define NETWORK_BACKEND 1
#endif

# ifndef MAX_FACTORIES
#define MAX_FACTORIES 10
#endif /* MAX_FACTORIES */

static struct iio_context_factory *context_factories[MAX_FACTORIES];
static unsigned factories_nb;

static int factory_set_property(struct iio_context_factory *factory,
		const char *key, const char *value)
{
	unsigned i;
	struct iio_property *p;

	if (!factory || !key || !*key || !value)
		return -EINVAL;

	for (i = 0; i < MAX_FACTORY_PROPERTIES; i++)
		if (!factory->properties[i].key)
			break;
		else if (!strcmp(factory->properties[i].key, key))
			return -EBADSLT;

	if (i > MAX_FACTORY_PROPERTIES) {
		ERROR("no room left for storing more factory properties");
		return -ENOMEM;
	}
	p = factory->properties + i;

	p->key = strdup(key);
	if (!p->key)
		return -ENOMEM;

	p->value = strdup(value);
	if (!p->value) {
		free(p->key);
		p->key = NULL;

		return -ENOMEM;
	}

	return 0;
}

static int factory_set_properties(struct iio_context_factory *factory,
		const struct iio_property *properties)
{
	int ret;
	const struct iio_property *p;

	if (!factory|| !properties || !properties[0].key)
		return -EINVAL;

	for (p = properties; p->key; p++) {
		ret = factory_set_property(factory, p->key, p->value);
		if (ret < 0) {
			ERROR("%s factory_set_property: %s\n", __func__,
					strerror(-ret));
			return ret;
		}
	}

	return 0;
}

static struct iio_context_factory * get_context_factory(const char *name)
{
	unsigned i;

	for (i = 0; i < MAX_FACTORIES; i++)
		if (!strcmp(context_factories[i]->name, name))
			return context_factories[i];

	errno = ENOSYS;

	return NULL;
}

struct iio_context * iio_create_context(const char *name,
		const struct iio_property *properties)
{
	int ret;
	struct iio_context_factory *factory;

	if (!name|| !*name) {
		errno = -EINVAL;
		return NULL;
	}

	factory = get_context_factory(name);
	if (!factory)
		return NULL;

	if (properties) {
		ret = factory_set_properties(factory, properties);
		if (ret < 0) {
			errno = -ret;
			return NULL;
		}
	}

	return factory->create_context();
}

int iio_context_factory_register(struct iio_context_factory *factory)
{
	if (!factory || !factory->create_context || !factory->name)
		return -EINVAL;

	DEBUG("%s \"%s\"\n", __func__, factory->name);

	if (factories_nb == MAX_FACTORIES)
		return -ENOMEM;
	context_factories[factories_nb] = factory;
	factories_nb++;

	return 0;
}

/* cleanup all the properties registered in a context factory */
static void cleanup_properties(struct iio_context_factory *factory)
{
	unsigned j;
	struct iio_property *p;

	/* cleanup all the properties registered in the factory */
	for (j = 0; j < MAX_FACTORY_PROPERTIES; j++) {
		p = factory->properties + j;
		if (p->key)
			free(p->key);
		if (p->value)
			free(p->value);
		memset(p, 0, sizeof(*p));
	}
}

int iio_context_factory_unregister(const char *name)
{
	unsigned i;

	if (!name)
		return -EINVAL;

	DEBUG("%s \"%s\"\n", __func__, name);

	for (i = 0; i < MAX_FACTORIES; i++)
		if (!strcmp(context_factories[i]->name, name))
			break;
	if (i == MAX_FACTORIES)
		return -ENOENT;

	cleanup_properties(context_factories[i]);

	factories_nb--;
	for (; i + 1 < MAX_FACTORIES; i++)
		context_factories[i] = context_factories[i + 1];
	memset(context_factories + i, 0, sizeof(*context_factories));

	return 0;
}

const char * iio_context_factory_get_property(
		struct iio_context_factory *factory, const char *key)
{
	unsigned i;

	if (!factory || !key || !*key) {
		errno = EINVAL;
		return NULL;
	}

	for (i = 0; i < MAX_FACTORY_PROPERTIES; i++)
		if (!factory->properties[i].key)
			break;
		else
			if (!strcmp(key, factory->properties[i].key))
				return factory->properties[i].value;

	errno = ENOENT;

	return NULL;
}

void iio_context_dump_factories(void)
{
	unsigned i;

	DEBUG("List of registered context factories:\n");

	for (i = 0; i < factories_nb; i++)
		DEBUG("\t\"%s\" %p\n", context_factories[i]->name,
				context_factories[i]->create_context);
}

static const char xml_header[] = "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
"<!DOCTYPE context ["
"<!ELEMENT context (device)*>"
"<!ELEMENT device (channel | attribute | debug-attribute)*>"
"<!ELEMENT channel (scan-element?, attribute*)>"
"<!ELEMENT attribute EMPTY>"
"<!ELEMENT scan-element EMPTY>"
"<!ELEMENT debug-attribute EMPTY>"
"<!ATTLIST context name CDATA #REQUIRED description CDATA #IMPLIED>"
"<!ATTLIST device id CDATA #REQUIRED name CDATA #IMPLIED>"
"<!ATTLIST channel id CDATA #REQUIRED type (input|output) #REQUIRED name CDATA #IMPLIED>"
"<!ATTLIST scan-element index CDATA #REQUIRED format CDATA #REQUIRED scale CDATA #IMPLIED>"
"<!ATTLIST attribute name CDATA #REQUIRED filename CDATA #IMPLIED>"
"<!ATTLIST debug-attribute name CDATA #REQUIRED>"
"]>";

/* Returns a string containing the XML representation of this context */
char * iio_context_create_xml(const struct iio_context *ctx)
{
	size_t len, *devices_len;
	char *str, *ptr, **devices;
	unsigned int i;

	len = strlen(ctx->name) + sizeof(xml_header) - 1 +
		sizeof("<context name=\"\" ></context>");
	if (ctx->description)
		len += strlen(ctx->description) +
			sizeof(" description=\"\"") - 1;

	if (!ctx->nb_devices) {
		str = malloc(len);
		if (!str) {
			errno = ENOMEM;
			return NULL;
		}

		if (ctx->description)
			snprintf(str, len, "%s<context name=\"%s\" "
					"description=\"%s\" ></context>",
					xml_header, ctx->name,
					ctx->description);
		else
			snprintf(str, len, "%s<context name=\"%s\" ></context>",
					xml_header, ctx->name);
		return str;
	}

	devices_len = malloc(ctx->nb_devices * sizeof(*devices_len));
	if (!devices_len) {
		errno = ENOMEM;
		return NULL;
	}

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
	if (!str) {
		errno = ENOMEM;
		goto err_free_devices;
	}

	if (ctx->description)
		snprintf(str, len, "%s<context name=\"%s\" "
				"description=\"%s\" >",
				xml_header, ctx->name, ctx->description);
	else
		snprintf(str, len, "%s<context name=\"%s\" >",
				xml_header, ctx->name);
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

	for (i = 0; i < ctx->nb_devices; i++)
		free_device(ctx->devices[i]);
	if (ctx->nb_devices)
		free(ctx->devices);
	if (ctx->xml)
		free(ctx->xml);
	if (ctx->description)
		free(ctx->description);
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

			if (ch2 >= 0 && ((ch1 > ch2) || ch1 < 0)) {
				struct iio_channel *bak = channels[i];
				channels[i] = channels[i - 1];
				channels[i - 1] = bak;
				found = true;
			}
		}
	} while (found);
}

void iio_context_init(struct iio_context *ctx)
{
	unsigned int i;
	for (i = 0; i < ctx->nb_devices; i++)
		reorder_channels(ctx->devices[i]);
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

struct iio_context * iio_create_default_context(void)
{
#if NETWORK_BACKEND
	char *hostname = getenv("IIOD_REMOTE");

	if (hostname) {
		/* If the environment variable is an empty string, we will
		 * discover the server using ZeroConf */
		if (strlen(hostname) == 0)
			hostname = NULL;

		return iio_create_network_context(hostname);
	}
#endif
	return iio_create_local_context();
}

struct iio_context * iio_create_local_context(void)
{
	const struct iio_context_factory *factory;

	factory = get_context_factory("local");
	if (!factory)
		return NULL;

	return factory->create_context();
}

struct iio_context * iio_create_network_context(const char *hostname)
{
	int ret;
	struct iio_context_factory *factory;

	factory = get_context_factory("network");
	if (!factory)
		return NULL;

	ret = factory_set_property(factory, "hostname", hostname);
	if (ret < 0) {
		errno = -ret;

		return 0;
	}

	return factory->create_context();
}

struct iio_context * iio_create_xml_context_mem(const char *xml, size_t len)
{
#if NETWORK_BACKEND
	return xml_create_context_mem(xml, len);
#else
	errno = ENOSYS;
	return NULL;
#endif
}

struct iio_context * iio_create_xml_context(const char *xml_file)
{
#if NETWORK_BACKEND
	return xml_create_context(xml_file);
#else
	errno = ENOSYS;
	return NULL;
#endif
}
