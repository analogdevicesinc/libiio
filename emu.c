// SPDX-License-Identifier: MIT
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 * Author: Marius Lucacel <marius.lucacel@analog.com>
 */

#include "iio-config.h"

#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <iio/iio-backend.h>
#include <iio/iio-debug.h>
#include <iio/iio-lock.h>
#include "iio-private.h"
#include <math.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

struct iio_context_pdata {
	xmlDoc *doc;
	char *xml_path;
};

struct iio_buffer_pdata {
	const struct iio_device *dev;
	unsigned int idx;
	FILE *file;
	struct iio_mutex *file_lock;
	struct iio_cond *file_ready;
	bool file_is_ready;
};

struct iio_block_pdata {
	struct iio_buffer_pdata *buf;
	void *data;
	size_t size;
	bool queued;
	size_t bytes_to_process;
};

static xmlNode *getNode(xmlNode *parent, const char *node_name,
						const char *attr_name, const char *attr_value)
{
	xmlNode *node;
	xmlAttr *attr;

	if (!parent)
		return NULL;

	for (node = parent->children; node; node = node->next) {
		if (strcmp((char *) node->name, node_name))
			continue;
		if (!attr_name)
			return node;

		for (attr = node->properties; attr; attr = attr->next) {
			if (strcmp((char *) attr->name, attr_name) == 0 &&
				strcmp((char *) attr->children->content, attr_value) == 0){
				return node;
			}
		}
	}
	return NULL;
}

static xmlNode *find_buffer_attribute(xmlNode *node_device, const char *attr)
{
	xmlNode *n;

	/* Prefer new format: <buffer><attribute name="attr_name"> (has values) */
	for (n = node_device->children; n; n = n->next) {
		if (strcmp((char *) n->name, "buffer"))
			continue;

		xmlNode *attr_node = getNode(n, "attribute", "name", attr);
		if (attr_node)
			return attr_node;
	}

	/* Fall back to legacy format: <buffer-attribute name="attr_name"> */
	return getNode(node_device, "buffer-attribute", "name", attr);
}

static xmlNode *getDeviceAttr(xmlDoc *doc, const char *device_id,
								enum iio_attr_type type, const char *attr)
{
	xmlNode *root, *node_device;
	const char *attr_type_name;
	if (!doc)
		return NULL;

	root = xmlDocGetRootElement(doc);
	if (!root)
		return NULL;

	node_device = getNode(root, "device", "id", device_id);
	if (!node_device)
		return NULL;

	switch (type) {
	case IIO_ATTR_TYPE_DEVICE:
		attr_type_name = "attribute";
		break;
	case IIO_ATTR_TYPE_DEBUG:
		attr_type_name = "debug-attribute";
		break;
	case IIO_ATTR_TYPE_BUFFER:
		/* Buffer attributes require special handling for both formats */
		return find_buffer_attribute(node_device, attr);
	default:
		return NULL;
	}
	return getNode(node_device, attr_type_name, "name", attr);
}

static xmlNode *getChannelAttr(xmlDoc *doc, const char *device_id,
								const char *channel_id, bool ch_out,
								const char *attr)
{
	xmlNode *root, *node_device, *node_channel;
	const char *channel_type;
	if (!doc)
		return NULL;

	root = xmlDocGetRootElement(doc);
	node_device = getNode(root, "device", "id", device_id);
	if (!node_device)
		return NULL;

	channel_type = ch_out ? "output" : "input";

	for (node_channel = node_device->children; node_channel; node_channel = node_channel->next) {
		if (strcmp((char *) node_channel->name, "channel"))
			continue;
		xmlAttr *prop;
		const char *id = NULL, *type = NULL;
		for (prop = node_channel->properties; prop; prop = prop->next) {
			if (strcmp((char *) prop->name, "id") == 0)
				id = (char *) prop->children->content;
			else if (strcmp((char *) prop->name, "type") == 0)
				type = (char *) prop->children->content;
		}
		if (id && type && strcmp(id, channel_id) == 0 && strcmp(type, channel_type) == 0) {
			return getNode(node_channel, "attribute", "name", attr);
		}
	}
	return NULL;
}

static ssize_t read_device_attr(xmlDoc *doc, const char *device_id, enum iio_attr_type type,
								const char *attr, char *buf, size_t len)
{
	char *value;
	xmlNode *node_attr = getDeviceAttr(doc, device_id, type, attr);
	if (!node_attr)
		return -ENOENT;
	value = (char *) xmlGetProp(node_attr, (const xmlChar *) "value");
	if (!value)
		return -ENOENT;

	iio_strlcpy(buf, value, len);
	xmlFree(value);

	return (ssize_t)strnlen(buf, len) + 1;
}

static ssize_t write_device_attr(xmlDoc *doc, const char *device_id,
				const char *attr, const char *buf, size_t len,
				enum iio_attr_type type)
{
	xmlNode *node_attr = getDeviceAttr(doc, device_id, type, attr);

	if (!node_attr)
		return -ENOENT;

	xmlSetProp(node_attr, (const xmlChar *) "value", (const xmlChar *) buf);

	return (ssize_t)len;
}

static ssize_t read_channel_attr(xmlDoc *doc, const char *device_id,
								const char *channel_id, bool ch_out,
								const char *attr, char *buf, size_t len)
{
	char *value;
	xmlNode *node_attr = getChannelAttr(doc, device_id, channel_id, ch_out, attr);
	if (!node_attr)
		return -ENOENT;
	value = (char *) xmlGetProp(node_attr, (const xmlChar *) "value");
	if (!value)
		return -ENOENT;

	iio_strlcpy(buf, value, len);
	xmlFree(value);

	return (ssize_t)strnlen(buf, len) + 1;
}

static ssize_t write_channel_attr(xmlDoc *doc, const char *device_id,
								const char *channel_id, bool ch_out,
								const char *attr, const char *buf, size_t len)
{
	xmlNode *node_attr = getChannelAttr(doc, device_id, channel_id, ch_out, attr);

	if (!node_attr)
		return -ENOENT;

	xmlSetProp(node_attr, (const xmlChar *) "value", (const xmlChar *) buf);

	return (ssize_t)len;
}

static int add_attr_to_device(struct iio_device *dev, xmlNode *n, enum iio_attr_type type)
{
	xmlAttr *attr;
	char *name = NULL;

	for (attr = n->properties; attr; attr = attr->next) {
		if (!strcmp((char *) attr->name, "name")) {
			name = (char *) attr->children->content;
		} else {
			dev_dbg(dev, "Unknown field \'%s\'\n", attr->name);
		}
	}

	if (!name) {
		dev_err(dev, "Incomplete attribute\n");
		return -EINVAL;
	}

	return iio_device_add_attr(dev, name, type);
}

static int add_attr_to_context(struct iio_context *ctx, xmlNode *n)
{
	xmlAttr *attr;
	char *name = NULL, *value = NULL;

	for (attr = n->properties; attr; attr = attr->next) {
		if (!strcmp((char *) attr->name, "name")) {
			name = (char *) attr->children->content;
		} else if (!strcmp((char *) attr->name, "value")) {
			value = (char *) attr->children->content;
		} else {
			ctx_dbg(ctx, "Unknown field \'%s\'\n", attr->name);
		}
	}

	if (!name || !value) {
		ctx_err(ctx, "Incomplete context attribute\n");
		return -EINVAL;
	}

	return iio_context_add_attr(ctx, name, value);
}

static int add_attr_to_buffer(struct iio_buffer *buf, xmlNode *n)
{
	xmlAttr *attr;
	const char *name = NULL;

	for (attr = n->properties; attr; attr = attr->next) {
		if (!strcmp((char *) attr->name, "name"))
			name = (char *) attr->children->content;
	}

	if (!name)
		return -EINVAL;

	return iio_buffer_add_attr(buf, name);
}

static int create_buffers(struct iio_device *dev, xmlNode *node)
{
	struct iio_buffer *buf;
	unsigned int idx = 0;
	xmlAttr *attr;
	xmlNode *n;
	int err;

	for (attr = node->properties; attr; attr = attr->next) {
		if (!strcmp((const char *) attr->name, "index"))
			idx = (unsigned int)strtoul(
				(const char *) attr->children->content, NULL, 10);
		else
			dev_dbg(dev, "Unknown attribute \'%s\' in <buffer>\n",
				attr->name);
	}

	buf = iio_device_add_buffer(dev, idx);
	if (!buf)
		return -ENOMEM;

	for (n = node->children; n; n = n->next) {
		if (!strcmp((const char *)n->name, "attribute")) {
			err = add_attr_to_buffer(buf, n);
			if (err < 0) {
				dev_err(dev, "Failed to add attribute to buffer%u (%d)\n",
					idx, err);
				return err;
			}
		} else {
			dev_dbg(dev, "Unknown children \'%s\' in <buffer>\n",
				n->name);
		}
	}

	return 0;
}

static int add_attr_to_channel(struct iio_channel *chn, xmlNode *n)
{
	const char *name = NULL, *filename = NULL;
	xmlAttr *attr;

	for (attr = n->properties; attr; attr = attr->next) {
		if (!strcmp((const char *) attr->name, "name")) {
			name = (const char *) attr->children->content;
		} else if (!strcmp((const char *) attr->name, "filename")) {
			filename = (const char *) attr->children->content;
		} else {
			chn_dbg(chn, "Unknown field \'%s\'\n", attr->name);
		}
	}

	if (!name) {
		chn_err(chn, "Incomplete attribute\n");
		return -EINVAL;
	}

	return iio_channel_add_attr(chn, name, filename);
}

static int setup_scan_element(const struct iio_device *dev,
			      xmlNode *n, long *index,
			      struct iio_data_format *fmt)
{
	xmlAttr *attr;
	int err;

	for (attr = n->properties; attr; attr = attr->next) {
		const char *name = (const char *) attr->name,
		      *content = (const char *) attr->children->content;
		if (!strcmp(name, "index")) {
			char *end;
			long long value;

			errno = 0;
			value = strtoll(content, &end, 0);
			if (end == content || value < 0 || errno == ERANGE)
				return -EINVAL;
			*index = (long)value;
		} else if (!strcmp(name, "format")) {
			char e, s;
			if (strchr(content, 'X')) {
				err = iio_sscanf(content, "%ce:%c%u/%uX%u>>%u",
#ifdef _MSC_BUILD
					&e, (unsigned int)sizeof(e),
					&s, (unsigned int)sizeof(s),
#else
					&e, &s,
#endif
					&fmt->bits,
					&fmt->length,
					&fmt->repeat,
					&fmt->shift);
				if (err != 6)
					return -EINVAL;
			} else {
				fmt->repeat = 1;
				err = iio_sscanf(content, "%ce:%c%u/%u>>%u",
#ifdef _MSC_BUILD
					&e, (unsigned int)sizeof(e),
					&s, (unsigned int)sizeof(s),
#else
					&e, &s,
#endif
					&fmt->bits,
					&fmt->length,
					&fmt->shift);
				if (err != 5)
					return -EINVAL;
			}
			fmt->is_be = e == 'b';
			fmt->is_signed = (s == 's' || s == 'S');
			fmt->is_fully_defined = (s == 'S' || s == 'U' ||
				fmt->bits == fmt->length);
		} else if (!strcmp(name, "scale")) {
			char *end;
			float value;

			errno = 0;
			value = strtof(content, &end);
			if (end == content || errno == ERANGE) {
				fmt->with_scale = false;
				return -EINVAL;
			}

			fmt->with_scale = true;
			fmt->scale = value;
		} else {
			dev_dbg(dev, "Unknown attribute \'%s\' in <scan-element>\n",
				name);
		}
	}

	return 0;
}

static int create_channel(struct iio_device *dev, xmlNode *node)
{
	xmlAttr *attr;
	struct iio_channel *chn;
	int err = -ENOMEM;
	char *name_ptr = NULL, *label_ptr = NULL, *id_ptr = NULL;
	bool output = false;
	bool scan_element = false;
	long index = -ENOENT;
	struct iio_data_format format = { 0 };
	xmlNode *n;

	for (attr = node->properties; attr; attr = attr->next) {
		const char *name = (const char *) attr->name,
		      *content = (const char *) attr->children->content;
		if (!strcmp(name, "name")) {
			name_ptr = iio_strdup(content);
			if (!name_ptr)
				goto err_free_name_id;
		} else if (!strcmp(name, "label")) {
			label_ptr = iio_strdup(content);
			if (!label_ptr)
				goto err_free_name_id;
		} else if (!strcmp(name, "id")) {
			id_ptr = iio_strdup(content);
			if (!id_ptr)
				goto err_free_name_id;
		} else if (!strcmp(name, "type")) {
			if (!strcmp(content, "output"))
				output = true;
			else if (strcmp(content, "input"))
				dev_dbg(dev, "Unknown channel type %s\n", content);
		} else {
			dev_dbg(dev, "Unknown attribute \'%s\' in <channel>\n",
				name);
		}
	}

	if (!id_ptr) {
		dev_err(dev, "Incomplete <channel>\n");
		err = -EINVAL;
		goto err_free_name_id;
	}

	for (n = node->children; n; n = n->next) {
		if (!strcmp((char *) n->name, "scan-element")) {
			scan_element = true;
			err = setup_scan_element(dev, n, &index, &format);
			if (err < 0)
				goto err_free_name_id;
			break;
		}
	}

	chn = iio_device_add_channel(dev, index, id_ptr, name_ptr, label_ptr,
						output, scan_element, &format);
	if (!chn) {
		err = -ENOMEM;
		goto err_free_name_id;
	}

	free(name_ptr);
	free(label_ptr);
	free(id_ptr);

	for (n = node->children; n; n = n->next) {
		if (!strcmp((char *) n->name, "attribute")) {
			err = add_attr_to_channel(chn, n);
			if (err < 0)
				return err;
		} else if (strcmp((char *) n->name, "scan-element")
			   && strcmp((char *) n->name, "text")) {
			chn_dbg(chn, "Unknown children \'%s\' in <channel>\n",
				n->name);
			continue;
		}
	}

	return 0;

err_free_name_id:
	free(name_ptr);
	free(label_ptr);
	free(id_ptr);
	return err;
}

static int create_device(struct iio_context *ctx, xmlNode *n)
{
	xmlAttr *attr;
	struct iio_device *dev;
	int err = -ENOMEM;
	char *name = NULL, *label = NULL, *id = NULL;
	bool buf_legacy = true;

	for (attr = n->properties; attr; attr = attr->next) {
		if (!strcmp((char *) attr->name, "name")) {
			name = iio_strdup((char *) attr->children->content);
			if (!name)
				goto err_free_name_label_id;
		} else if (!strcmp((char *) attr->name, "label")) {
			label = iio_strdup((char *) attr->children->content);
			if (!label)
				goto err_free_name_label_id;
		} else if (!strcmp((char *) attr->name, "id")) {
			id = iio_strdup((char *) attr->children->content);
			if (!id)
				goto err_free_name_label_id;
		} else {
			ctx_dbg(ctx, "Unknown attribute \'%s\' in <device>\n",
				attr->name);
		}
	}

	if (!id) {
		ctx_err(ctx, "Unable to read device ID\n");
		err = -EINVAL;
		goto err_free_name_label_id;
	}

	dev = iio_context_add_device(ctx, id, name, label);
	if (!dev)
		goto err_free_name_label_id;

	free(name);
	free(label);
	free(id);

	for (n = n->children; n; n = n->next) {
		if (!strcmp((char *) n->name, "channel")) {
			err = create_channel(dev, n);
			if (err) {
				dev_perror(dev, err, "Unable to create channel");
				return err;
			}
		} else if (!strcmp((char *) n->name, "buffer")) {
			buf_legacy = false;
			err = create_buffers(dev, n);
			if (err < 0)
				return err;
		} else if (!strcmp((char *) n->name, "attribute")) {
			err = add_attr_to_device(dev, n, IIO_ATTR_TYPE_DEVICE);
			if (err < 0)
				return err;
		} else if (!strcmp((char *) n->name, "debug-attribute")) {
			err = add_attr_to_device(dev, n, IIO_ATTR_TYPE_DEBUG);
			if (err < 0)
				return err;
		} else if (!strcmp((char *) n->name, "buffer-attribute") && buf_legacy) {
			struct iio_buffer *buf = iio_device_get_buffer(dev, 0);

			if (!buf) {
				buf = iio_device_add_buffer(dev, 0);
				if (!buf)
					return -ENOMEM;
			}

			err = add_attr_to_buffer(buf, n);
			if (err < 0)
				return err;
		} else if (strcmp((char *) n->name, "text") && strcmp((char *) n->name, "buffer-attribute")) {
			dev_dbg(dev, "Unknown children \'%s\' in <device>\n",
				n->name);
			continue;
		}
	}

	return 0;

err_free_name_label_id:
	free(name);
	free(label);
	free(id);
	return err;
}

static struct iio_context *
emu_create_context(const struct iio_context_params *params, const char *args)
{
	struct iio_context *ctx;
	struct iio_context_pdata *pdata;
	xmlDoc *doc;
	xmlNode *root, *node;
	int ret;

	if (!args || !*args) {
		prm_err(params, "EMU backend requires XML file path\n");
		prm_err(params, "Usage: emu:/path/to/device.xml\n");
		return iio_ptr(-EINVAL);
	}

	LIBXML_TEST_VERSION;
	doc = xmlReadFile(args, NULL, XML_PARSE_DTDVALID);
	if (!doc) {
		prm_err(params, "Unable to parse XML file: %s\n", args);
		return iio_ptr(-EINVAL);
	}

	root = xmlDocGetRootElement(doc);
	if (!root || strcmp((char *) root->name, "context")) {
		prm_err(params, "Invalid XML: root must be context\n");
		xmlFreeDoc(doc);
		return iio_ptr(-EINVAL);
	}

	ctx = iio_context_create_from_backend(params, &iio_emu_backend,
										"Emulated IIO Context", 0, 1, "emu-v1");

	ret = iio_err(ctx);
	if (ret) {
		prm_err(params, "Failed to create context\n");
		xmlFreeDoc(doc);
		return iio_ptr(ret);
	}

	for (node = root->children; node; node = node->next) {
		if (!strcmp((char *) node->name, "device")) {
			ret = create_device(ctx, node);
			if (ret) {
				ctx_perror(ctx, ret, "Unable to create device");
				iio_context_destroy(ctx);
				xmlFreeDoc(doc);
				return iio_ptr(ret);
			}
		} else if (!strcmp((char *) node->name, "context-attribute")) {
			ret = add_attr_to_context(ctx, node);
			if (ret) {
				ctx_perror(ctx, ret, "Unable to create context attribute");
				iio_context_destroy(ctx);
				xmlFreeDoc(doc);
				return iio_ptr(ret);
			}
		} else if (strcmp((char *) node->name, "text")) {
			ctx_dbg(ctx, "Unknown children \'%s\' in <context>\n",
				node->name);
			continue;
		}
	}
	pdata = zalloc(sizeof(*pdata));
	if (!pdata) {
		iio_context_destroy(ctx);
		xmlFreeDoc(doc);
		return iio_ptr(-ENOMEM);
	}

	pdata->doc = doc;
	pdata->xml_path = iio_strdup(args);

	iio_context_set_pdata(ctx, pdata);

	ctx_info(ctx, "Emulated context created from %s\n", args);

	return ctx;
}

static ssize_t emu_read_attr(const struct iio_attr *attr, char *dst, size_t len)
{
	const struct iio_device *dev;
	const struct iio_context *ctx;
	struct iio_context_pdata *pdata;
	const char *device_id, *attr_name;

	dev = iio_attr_get_device(attr);
	if (!dev)
		return -EINVAL;
	ctx = dev->ctx;
	pdata = iio_context_get_pdata(ctx);
	if (!pdata)
		return -EINVAL;
	device_id = iio_device_get_id(dev);
	attr_name = iio_attr_get_name(attr);

	if (attr->type == IIO_ATTR_TYPE_CHANNEL) {
		const struct iio_channel *chn = attr->iio.chn;
		if (!chn)
			return -EINVAL;
		return read_channel_attr(pdata->doc, device_id, iio_channel_get_id(chn),
									iio_channel_is_output(chn), attr_name, dst, len);
	}

	return read_device_attr(pdata->doc, device_id, attr->type, attr_name, dst, len);
}

static int validate_against_available(const struct iio_attr *attr, const char *src)
{
	double min = 0, step = 0, max = 0;
	char **list = NULL;
	size_t count = 0;
	bool found;
	int ret;

	ret = iio_attr_get_range(attr, &min, &step, &max);
	if (ret == 0) {
		double value, steps_from_min;
		if (step == 0 || iio_sscanf(src, "%lf", &value) != 1 ||
		    value < min || value > max)
			return -EINVAL;
		steps_from_min = (value - min) / step;
		if (fabs(steps_from_min - round(steps_from_min)) > 1e-9)
			return -EINVAL;
		return 0;
	}

	if (ret == -EOPNOTSUPP) {
		ret = iio_attr_get_available(attr, &list, &count);
		if (ret < 0)
			return ret;
		found = false;
		for (size_t i = 0; i < count; i++) {
			if (strcmp(src, list[i]) == 0) {
				found = true;
				break;
			}
		}
		iio_available_list_free(list, count);
		return found ? 0 : -EINVAL;
	}

	return 0;
}

static int check_available(const struct iio_attr *attr, const char *src)
{
	char avail_name[MAX_ATTR_NAME + sizeof("_available")];
	const struct iio_attr *avail_attr;

	snprintf(avail_name, sizeof(avail_name), "%s_available", iio_attr_get_name(attr));

	switch (attr->type) {
	case IIO_ATTR_TYPE_CHANNEL:
		avail_attr = iio_channel_find_attr(attr->iio.chn, avail_name);
		break;
	case IIO_ATTR_TYPE_DEVICE:
		avail_attr = iio_device_find_attr(attr->iio.dev, avail_name);
		break;
	case IIO_ATTR_TYPE_DEBUG:
		avail_attr = iio_device_find_debug_attr(attr->iio.dev, avail_name);
		break;
	default:
		return 0;
	}

	if (!avail_attr)
		return 0;

	return validate_against_available(avail_attr, src);
}

static ssize_t emu_write_attr(const struct iio_attr *attr, const char *src, size_t len)
{
	const struct iio_device *dev;
	const struct iio_context *ctx;
	struct iio_context_pdata *pdata;
	const char *device_id, *attr_name;

	dev = iio_attr_get_device(attr);
	if (!dev)
		return -EINVAL;
	ctx = dev->ctx;
	pdata = iio_context_get_pdata(ctx);
	if (!pdata)
		return -EINVAL;
	device_id = iio_device_get_id(dev);
	attr_name = iio_attr_get_name(attr);

	if (string_ends_with(attr_name, "_available"))
		return -EACCES;

	if (check_available(attr, src) < 0)
		return -EINVAL;

	if (attr->type == IIO_ATTR_TYPE_CHANNEL) {
		const struct iio_channel *chn = attr->iio.chn;
		if (!chn)
			return -EINVAL;
		return write_channel_attr(pdata->doc, device_id, iio_channel_get_id(chn),
									iio_channel_is_output(chn), attr_name, src, len);
	}

	return write_device_attr(pdata->doc, device_id, attr_name, src, len, attr->type);
}

static void emu_shutdown(struct iio_context *ctx)
{
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);

	if (!pdata)
		return;

	if (pdata->doc) {
		xmlFreeDoc(pdata->doc);
		pdata->doc = NULL;
	}

	if (pdata->xml_path) {
		free(pdata->xml_path);
		pdata->xml_path = NULL;
	}
}

/* Returns a heap-allocated path string; caller must free().
 * Returns NULL on allocation failure.
 * Cross-platform: handles both '/' and '\\' directory separators. */
static char *emu_make_data_path(const char *xml_path,
				const char *dev_id,
				unsigned int idx)
{
	const char *sep = strrchr(xml_path, '/');
#ifdef _WIN32
	const char *sep_win = strrchr(xml_path, '\\');
	if (sep_win && (!sep || sep_win > sep))
		sep = sep_win;
#endif
	size_t dir_len = sep ? (size_t)(sep - xml_path + 1) : 0;
	size_t path_len = dir_len + MAX_DEV_ID + 32; /* _buf{idx}.bin + margin */
	char *path = malloc(path_len);

	if (!path)
		return NULL;

	if (dir_len)
		iio_snprintf(path, path_len, "%.*s%s_buf%u.bin", (int)dir_len, xml_path, dev_id, idx);
	else
		iio_snprintf(path, path_len, "%s_buf%u.bin", dev_id, idx);

	return path;
}

static struct iio_buffer_pdata *
emu_open_buffer(const struct iio_device *dev, unsigned int idx,
		struct iio_channels_mask *mask)
{
	struct iio_buffer_pdata *pdata = zalloc(sizeof(*pdata));

	if (!pdata)
		return iio_ptr(-ENOMEM);

	pdata->dev = dev;
	pdata->idx = idx;
	pdata->file = NULL;
	pdata->file_is_ready = false;

	pdata->file_lock = iio_mutex_create();
	if (iio_err(pdata->file_lock)) {
		free(pdata);
		return iio_ptr(-ENOMEM);
	}

	pdata->file_ready = iio_cond_create();
	if (iio_err(pdata->file_ready)) {
		iio_mutex_destroy(pdata->file_lock);
		free(pdata);
		return iio_ptr(-ENOMEM);
	}

	return pdata;
}

static void emu_close_buffer(struct iio_buffer_pdata *pdata)
{
	if (pdata->file)
		fclose(pdata->file);

	if (pdata->file_ready)
		iio_cond_destroy(pdata->file_ready);
	if (pdata->file_lock)
		iio_mutex_destroy(pdata->file_lock);

	free(pdata);
}

static int emu_enable_buffer(struct iio_buffer_pdata *pdata,
			     size_t nb_samples, bool enable, bool cyclic)
{
	struct iio_context_pdata *ctx_pdata;
	const struct iio_buffer *buf;
	char *path;
	bool is_output;

	iio_mutex_lock(pdata->file_lock);

	if (pdata->file) {
		fclose(pdata->file);
		pdata->file = NULL;
		pdata->file_is_ready = false;
	}

	if (!enable) {
		iio_mutex_unlock(pdata->file_lock);
		return 0;
	}

	buf = iio_device_get_buffer(pdata->dev, pdata->idx);
	is_output = iio_buffer_is_output(buf);

	ctx_pdata = iio_context_get_pdata(pdata->dev->ctx);
	path = emu_make_data_path(ctx_pdata->xml_path,
				  iio_device_get_id(pdata->dev),
				  pdata->idx);
	if (!path) {
		iio_mutex_unlock(pdata->file_lock);
		return -ENOMEM;
	}

	pdata->file = fopen(path, is_output ? "wb" : "rb");
	if (!pdata->file) {
		if(!is_output)
			dev_err(pdata->dev, "Rx data file not found, expected at %s\n", path);
		free(path);
		iio_mutex_unlock(pdata->file_lock);
		return is_output ? -EBADF : -ENOENT;
	}
	free(path);

	pdata->file_is_ready = true;
	iio_cond_signal(pdata->file_ready);
	iio_mutex_unlock(pdata->file_lock);

	return 0;
}

static void emu_cancel_buffer(struct iio_buffer_pdata *pdata)
{
}

static struct iio_block_pdata *
emu_create_block(struct iio_buffer_pdata *pdata, size_t size, void **data)
{
	struct iio_block_pdata *block = zalloc(sizeof(*block));

	if (!block)
		return iio_ptr(-ENOMEM);

	block->data = malloc(size);
	if (!block->data) {
		free(block);
		return iio_ptr(-ENOMEM);
	}

	block->buf = pdata;
	block->size = size;
	*data = block->data;
	return block;
}

static void emu_free_block(struct iio_block_pdata *pdata)
{
	free(pdata->data);
	free(pdata);
}

static int emu_enqueue_block(struct iio_block_pdata *pdata,
			     size_t bytes_used, bool cyclic)
{
	if (pdata->queued)
		return -EBUSY;

	pdata->queued = true;
	pdata->bytes_to_process = bytes_used;
	return 0;
}

static int emu_dequeue_block(struct iio_block_pdata *pdata, bool nonblock)
{
	FILE *file;
	const struct iio_buffer *buf;
	bool is_output;
	size_t n, n2;
	size_t bytes_used;

	if (!pdata->queued)
		return -EINVAL;

	iio_mutex_lock(pdata->buf->file_lock);
	while (!pdata->buf->file_is_ready) {
		int ret = iio_cond_wait(pdata->buf->file_ready,
					pdata->buf->file_lock, 5000);
		if (ret) {
			iio_mutex_unlock(pdata->buf->file_lock);
			return -EBADF;
		}
	}
	file = pdata->buf->file;
	iio_mutex_unlock(pdata->buf->file_lock);

	bytes_used = pdata->bytes_to_process ? pdata->bytes_to_process : pdata->size;

	buf = iio_device_get_buffer(pdata->buf->dev, pdata->buf->idx);
	is_output = iio_buffer_is_output(buf);

	if (is_output) {
		/* TX: always overwrite from the start */
		fseek(file, 0, SEEK_SET);
		n = fwrite(pdata->data, 1, bytes_used, file);
		fflush(file);
		if (n < bytes_used)
			return -EIO;

		/* Truncate to remove any stale bytes from a previous larger write */
#ifdef _WIN32
		if (_chsize(_fileno(file), (long)ftell(file)) != 0)
			return -EIO;
#else
		if (ftruncate(fileno(file), ftell(file)) != 0)
			return -EIO;
#endif
	} else {
		/* RX: read from current file position, naturally advances */
		n = fread(pdata->data, 1, bytes_used, file);

		if (n < bytes_used) {
			fseek(file, 0, SEEK_SET);
			n2 = fread((char*)pdata->data + n, 1, bytes_used - n, file);
			if (n2 == 0)
				return -ENODATA;
		}
	}

	pdata->queued = false;
	return 0;
}

static const struct iio_backend_ops emu_ops = {
	.create = emu_create_context,
	.read_attr = emu_read_attr,
	.write_attr = emu_write_attr,
	.shutdown = emu_shutdown,

	.open_buffer   = emu_open_buffer,
	.close_buffer  = emu_close_buffer,
	.enable_buffer = emu_enable_buffer,
	.cancel_buffer = emu_cancel_buffer,

	.create_block  = emu_create_block,
	.free_block    = emu_free_block,
	.enqueue_block = emu_enqueue_block,
	.dequeue_block = emu_dequeue_block,
};

const struct iio_backend iio_emu_backend = {
	.api_version = IIO_BACKEND_API_V1,
	.name = "emu",
	.uri_prefix = "emu:",
	.ops = &emu_ops,
	.default_timeout_ms = 1000,
};

void libiio_cleanup_emu_backend(void)
{
	/*
	 * This function is called only when the libiio library is
	 * unloaded (e.g. when the program exits).
	 *
	 * Cleanup libxml2 so that memory analyzer tools like Valgrind won't
	 * detect a memory leak.
	 */
	xmlCleanupParser();
#if LIBXML_VERSION < 21200  /* Version 2.12.0 */
	xmlMemoryDump();
#endif
}
