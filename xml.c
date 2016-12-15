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
#include <libxml/tree.h>
#include <string.h>

static int add_attr_to_channel(struct iio_channel *chn, xmlNode *n)
{
	xmlAttr *attr;
	char *name = NULL, *filename = NULL;
	struct iio_channel_attr *attrs;

	for (attr = n->properties; attr; attr = attr->next) {
		if (!strcmp((char *) attr->name, "name")) {
			name = iio_strdup((char *) attr->children->content);
		} else if (!strcmp((char *) attr->name, "filename")) {
			filename = iio_strdup((char *) attr->children->content);
		} else {
			WARNING("Unknown field \'%s\' in channel %s\n",
					attr->name, chn->id);
		}
	}

	if (!name) {
		ERROR("Incomplete attribute in channel %s\n", chn->id);
		goto err_free;
	}

	if (!filename) {
		filename = iio_strdup(name);
		if (!filename)
			goto err_free;
	}

	attrs = realloc(chn->attrs, (1 + chn->nb_attrs) *
			sizeof(struct iio_channel_attr));
	if (!attrs)
		goto err_free;

	attrs[chn->nb_attrs].filename = filename;
	attrs[chn->nb_attrs++].name = name;
	chn->attrs = attrs;
	return 0;

err_free:
	if (name)
		free(name);
	if (filename)
		free(filename);
	return -1;
}

static int add_attr_to_device(struct iio_device *dev, xmlNode *n, bool is_debug)
{
	xmlAttr *attr;
	char **attrs, *name = NULL;

	for (attr = n->properties; attr; attr = attr->next) {
		if (!strcmp((char *) attr->name, "name")) {
			name = iio_strdup((char *) attr->children->content);
		} else {
			WARNING("Unknown field \'%s\' in device %s\n",
					attr->name, dev->id);
		}
	}

	if (!name) {
		ERROR("Incomplete attribute in device %s\n", dev->id);
		goto err_free;
	}

	if (is_debug)
		attrs = realloc(dev->debug_attrs,
				(1 + dev->nb_debug_attrs) * sizeof(char *));
	else
		attrs = realloc(dev->attrs,
				(1 + dev->nb_attrs) * sizeof(char *));
	if (!attrs)
		goto err_free;

	if (is_debug) {
		attrs[dev->nb_debug_attrs++] = name;
		dev->debug_attrs = attrs;
	} else {
		attrs[dev->nb_attrs++] = name;
		dev->attrs = attrs;
	}
	return 0;

err_free:
	if (name)
		free(name);
	return -1;
}

static void setup_scan_element(struct iio_channel *chn, xmlNode *n)
{
	xmlAttr *attr;

	for (attr = n->properties; attr; attr = attr->next) {
		const char *name = (const char *) attr->name,
		      *content = (const char *) attr->children->content;
		if (!strcmp(name, "index")) {
			chn->index = atol(content);
		} else if (!strcmp(name, "format")) {
			char e, s;
			if (strchr(content, 'X')) {
				sscanf(content, "%ce:%c%u/%uX%u>>%u", &e, &s,
					&chn->format.bits,
					&chn->format.length,
					&chn->format.repeat,
					&chn->format.shift);
			} else {
				chn->format.repeat = 1;
				sscanf(content, "%ce:%c%u/%u>>%u", &e, &s,
					&chn->format.bits,
					&chn->format.length,
					&chn->format.shift);
			}
			chn->format.is_be = e == 'b';
			chn->format.is_signed = (s == 's' || s == 'S');
			chn->format.is_fully_defined = (s == 'S' || s == 'U' ||
				chn->format.bits == chn->format.length);
		} else if (!strcmp(name, "scale")) {
			chn->format.with_scale = true;
			chn->format.scale = atof(content);
		} else {
			WARNING("Unknown attribute \'%s\' in <scan-element>\n",
					name);
		}
	}
}

static struct iio_channel * create_channel(struct iio_device *dev, xmlNode *n)
{
	xmlAttr *attr;
	struct iio_channel *chn = zalloc(sizeof(*chn));
	if (!chn)
		return NULL;

	chn->dev = dev;

	/* Set the default index value < 0 (== no index) */
	chn->index = -ENOENT;

	for (attr = n->properties; attr; attr = attr->next) {
		const char *name = (const char *) attr->name,
		      *content = (const char *) attr->children->content;
		if (!strcmp(name, "name")) {
			chn->name = iio_strdup(content);
		} else if (!strcmp(name, "id")) {
			chn->id = iio_strdup(content);
		} else if (!strcmp(name, "type")) {
			if (!strcmp(content, "output"))
				chn->is_output = true;
			else if (strcmp(content, "input"))
				WARNING("Unknown channel type %s\n", content);
		} else {
			WARNING("Unknown attribute \'%s\' in <channel>\n",
					name);
		}
	}

	if (!chn->id) {
		ERROR("Incomplete <attribute>\n");
		goto err_free_channel;
	}

	for (n = n->children; n; n = n->next) {
		if (!strcmp((char *) n->name, "attribute")) {
			if (add_attr_to_channel(chn, n) < 0)
				goto err_free_channel;
		} else if (!strcmp((char *) n->name, "scan-element")) {
			chn->is_scan_element = true;
			setup_scan_element(chn, n);
		} else if (strcmp((char *) n->name, "text")) {
			WARNING("Unknown children \'%s\' in <channel>\n",
					n->name);
			continue;
		}
	}

	iio_channel_init_finalize(chn);

	return chn;

err_free_channel:
	free_channel(chn);
	return NULL;
}

static struct iio_device * create_device(struct iio_context *ctx, xmlNode *n)
{
	xmlAttr *attr;
	struct iio_device *dev = zalloc(sizeof(*dev));
	if (!dev)
		return NULL;

	dev->ctx = ctx;

	for (attr = n->properties; attr; attr = attr->next) {
		if (!strcmp((char *) attr->name, "name")) {
			dev->name = iio_strdup(
					(char *) attr->children->content);
		} else if (!strcmp((char *) attr->name, "id")) {
			dev->id = iio_strdup((char *) attr->children->content);
		} else {
			WARNING("Unknown attribute \'%s\' in <device>\n",
					attr->name);
		}
	}

	if (!dev->id) {
		ERROR("Unable to read device ID\n");
		goto err_free_device;
	}

	for (n = n->children; n; n = n->next) {
		if (!strcmp((char *) n->name, "channel")) {
			struct iio_channel **chns,
					   *chn = create_channel(dev, n);
			if (!chn) {
				ERROR("Unable to create channel\n");
				goto err_free_device;
			}

			chns = realloc(dev->channels, (1 + dev->nb_channels) *
					sizeof(struct iio_channel *));
			if (!chns) {
				ERROR("Unable to allocate memory\n");
				free(chn);
				goto err_free_device;
			}

			chns[dev->nb_channels++] = chn;
			dev->channels = chns;
		} else if (!strcmp((char *) n->name, "attribute")) {
			if (add_attr_to_device(dev, n, false) < 0)
				goto err_free_device;
		} else if (!strcmp((char *) n->name, "debug-attribute")) {
			if (add_attr_to_device(dev, n, true) < 0)
				goto err_free_device;
		} else if (strcmp((char *) n->name, "text")) {
			WARNING("Unknown children \'%s\' in <device>\n",
					n->name);
			continue;
		}
	}

	dev->words = (dev->nb_channels + 31) / 32;
	if (dev->words) {
		dev->mask = calloc(dev->words, sizeof(*dev->mask));
		if (!dev->mask) {
			errno = ENOMEM;
			goto err_free_device;
		}
	}

	return dev;

err_free_device:
	free_device(dev);
	return NULL;
}

static struct iio_context * xml_clone(const struct iio_context *ctx)
{
	return xml_create_context_mem(ctx->xml, strlen(ctx->xml));
}

static const struct iio_backend_ops xml_ops = {
	.clone = xml_clone,
};

static int parse_context_attr(struct iio_context *ctx, xmlNode *n)
{
	xmlAttr *attr;
	const char *name = NULL, *value = NULL;

	for (attr = n->properties; attr; attr = attr->next) {
		if (!strcmp((const char *) attr->name, "name")) {
			name = (const char *) attr->children->content;
		} else if (!strcmp((const char *) attr->name, "value")) {
			value = (const char *) attr->children->content;
		}
	}

	if (!name || !value)
		return -EINVAL;
	else
		return iio_context_add_attr(ctx, name, value);
}

static struct iio_context * iio_create_xml_context_helper(xmlDoc *doc)
{
	unsigned int i;
	xmlNode *root, *n;
	xmlAttr *attr;
	int err = -ENOMEM;
	struct iio_context *ctx = zalloc(sizeof(*ctx));
	if (!ctx)
		goto err_set_errno;

	ctx->name = "xml";
	ctx->ops = &xml_ops;

	root = xmlDocGetRootElement(doc);
	if (strcmp((char *) root->name, "context")) {
		ERROR("Unrecognized XML file\n");
		err = -EINVAL;
		goto err_free_ctx;
	}

	for (attr = root->properties; attr; attr = attr->next) {
		if (!strcmp((char *) attr->name, "description"))
			ctx->description = iio_strdup(
					(char *) attr->children->content);
		else if (strcmp((char *) attr->name, "name"))
			WARNING("Unknown parameter \'%s\' in <context>\n",
					(char *) attr->children->content);
	}

	for (n = root->children; n; n = n->next) {
		struct iio_device **devs, *dev;

		if (!strcmp((char *) n->name, "context-attribute")) {
			err = parse_context_attr(ctx, n);
			if (err)
				goto err_free_devices;
			else
				continue;
		} else if (strcmp((char *) n->name, "device")) {
			if (strcmp((char *) n->name, "text"))
				WARNING("Unknown children \'%s\' in "
						"<context>\n", n->name);
			continue;
		}

		dev = create_device(ctx, n);
		if (!dev) {
			ERROR("Unable to create device\n");
			goto err_free_devices;
		}

		devs = realloc(ctx->devices, (1 + ctx->nb_devices) *
				sizeof(struct iio_device *));
		if (!devs) {
			ERROR("Unable to allocate memory\n");
			free(dev);
			goto err_free_devices;
		}

		devs[ctx->nb_devices++] = dev;
		ctx->devices = devs;
	}

	err = iio_context_init(ctx);
	if (err)
		goto err_free_devices;

	return ctx;

err_free_devices:
	for (i = 0; i < ctx->nb_devices; i++)
		free_device(ctx->devices[i]);
	if (ctx->nb_devices)
		free(ctx->devices);
	for (i = 0; i < ctx->nb_attrs; i++) {
		free(ctx->attrs[i]);
		free(ctx->values[i]);
	}
	free(ctx->attrs);
	free(ctx->values);
err_free_ctx:
	free(ctx);
err_set_errno:
	errno = -err;
	return NULL;
}

struct iio_context * xml_create_context(const char *xml_file)
{
	struct iio_context *ctx;
	xmlDoc *doc;

	LIBXML_TEST_VERSION;

	doc = xmlReadFile(xml_file, NULL, XML_PARSE_DTDVALID);
	if (!doc) {
		ERROR("Unable to parse XML file\n");
		errno = EINVAL;
		return NULL;
	}

	ctx = iio_create_xml_context_helper(doc);
	xmlFreeDoc(doc);
	return ctx;
}

struct iio_context * xml_create_context_mem(const char *xml, size_t len)
{
	struct iio_context *ctx;
	xmlDoc *doc;

	LIBXML_TEST_VERSION;

	doc = xmlReadMemory(xml, (int) len, NULL, NULL, XML_PARSE_DTDVALID);
	if (!doc) {
		ERROR("Unable to parse XML file\n");
		errno = EINVAL;
		return NULL;
	}

	ctx = iio_create_xml_context_helper(doc);
	xmlFreeDoc(doc);
	return ctx;
}
