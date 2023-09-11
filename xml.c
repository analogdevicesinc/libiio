// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "attr.h"
#include "iio-private.h"

#include <errno.h>
#include <iio/iio-debug.h>
#include <libxml/tree.h>
#include <string.h>

#define XML_HEADER "<?xml version=\"1.0\" encoding=\"utf-8\"?>"

static struct iio_context *
xml_create_context(const struct iio_context_params *params,
		   const char *xml_file);

static int add_attr_to_channel(struct iio_channel *chn, xmlNode *n)
{
	const char *name = NULL, *filename = NULL;
	xmlAttr *attr;

	for (attr = n->properties; attr; attr = attr->next) {
		if (!strcmp((const char *)attr->name, "name")) {
			name = (const char *)attr->children->content;
		} else if (!strcmp((const char *)attr->name, "filename")) {
			filename = (const char *)attr->children->content;
		} else {
			chn_dbg(chn, "Unknown field \'%s\' in channel %s\n",
				attr->name, chn->id);
		}
	}

	if (!name) {
		chn_err(chn, "Incomplete attribute in channel %s\n", chn->id);
		return -EINVAL;
	}

	return iio_channel_add_attr(chn, name, filename);
}

static int add_attr_to_device(struct iio_device *dev, xmlNode *n, enum iio_attr_type type)
{
	xmlAttr *attr;
	char *name = NULL;

	for (attr = n->properties; attr; attr = attr->next) {
		if (!strcmp((char *) attr->name, "name")) {
			name = (char *) attr->children->content;
		} else {
			dev_dbg(dev, "Unknown field \'%s\' in device %s\n",
				attr->name, dev->id);
		}
	}

	if (!name) {
		dev_err(dev, "Incomplete attribute in device %s\n", dev->id);
		return -EINVAL;
	}

	return iio_device_add_attr(dev, name, type);
}

static int setup_scan_element(struct iio_channel *chn, xmlNode *n)
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
			chn->index = (long) value;
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
					&chn->format.bits,
					&chn->format.length,
					&chn->format.repeat,
					&chn->format.shift);
				if (err != 6)
					return -EINVAL;
			} else {
				chn->format.repeat = 1;
				err = iio_sscanf(content, "%ce:%c%u/%u>>%u",
#ifdef _MSC_BUILD
					&e, (unsigned int)sizeof(e),
					&s, (unsigned int)sizeof(s),
#else
					&e, &s,
#endif
					&chn->format.bits,
					&chn->format.length,
					&chn->format.shift);
				if (err != 5)
					return -EINVAL;
			}
			chn->format.is_be = e == 'b';
			chn->format.is_signed = (s == 's' || s == 'S');
			chn->format.is_fully_defined = (s == 'S' || s == 'U' ||
				chn->format.bits == chn->format.length);
		} else if (!strcmp(name, "scale")) {
			char *end;
			float value;

			errno = 0;
			value = strtof(content, &end);
			if (end == content || errno == ERANGE) {
				chn->format.with_scale = false;
				return -EINVAL;
			}

			chn->format.with_scale = true;
			chn->format.scale = value;
		} else {
			chn_dbg(chn, "Unknown attribute \'%s\' in <scan-element>\n",
				name);
		}
	}

	return 0;
}

static struct iio_channel * create_channel(struct iio_device *dev, xmlNode *n)
{
	xmlAttr *attr;
	struct iio_channel *chn;
	int err = -ENOMEM;

	chn = zalloc(sizeof(*chn));
	if (!chn)
		return iio_ptr(-ENOMEM);

	chn->dev = dev;

	/* Set the default index value < 0 (== no index) */
	chn->index = -ENOENT;

	for (attr = n->properties; attr; attr = attr->next) {
		const char *name = (const char *) attr->name,
		      *content = (const char *) attr->children->content;
		if (!strcmp(name, "name")) {
			chn->name = iio_strdup(content);
			if (!chn->name)
				goto err_free_channel;
		} else if (!strcmp(name, "id")) {
			chn->id = iio_strdup(content);
			if (!chn->id)
				goto err_free_channel;
		} else if (!strcmp(name, "type")) {
			if (!strcmp(content, "output"))
				chn->is_output = true;
			else if (strcmp(content, "input"))
				dev_dbg(dev, "Unknown channel type %s\n", content);
		} else {
			dev_dbg(dev, "Unknown attribute \'%s\' in <channel>\n",
				name);
		}
	}

	if (!chn->id) {
		dev_err(dev, "Incomplete <attribute>\n");
		err = -EINVAL;
		goto err_free_channel;
	}

	for (n = n->children; n; n = n->next) {
		if (!strcmp((char *) n->name, "attribute")) {
			err = add_attr_to_channel(chn, n);
			if (err < 0)
				goto err_free_channel;
		} else if (!strcmp((char *) n->name, "scan-element")) {
			chn->is_scan_element = true;
			err = setup_scan_element(chn, n);
			if (err < 0)
				goto err_free_channel;
		} else if (strcmp((char *) n->name, "text")) {
			dev_dbg(dev, "Unknown children \'%s\' in <channel>\n",
				n->name);
			continue;
		}
	}

	iio_channel_init_finalize(chn);

	return chn;

err_free_channel:
	free_channel(chn);
	return iio_ptr(err);
}

static struct iio_device * create_device(struct iio_context *ctx, xmlNode *n)
{
	xmlAttr *attr;
	struct iio_device *dev;
	int err = -ENOMEM;

	dev = zalloc(sizeof(*dev));
	if (!dev)
		return iio_ptr(-ENOMEM);

	dev->ctx = ctx;

	for (attr = n->properties; attr; attr = attr->next) {
		if (!strcmp((char *) attr->name, "name")) {
			dev->name = iio_strdup(
					(char *) attr->children->content);
			if (!dev->name)
				goto err_free_device;
		} else if (!strcmp((char *) attr->name, "label")) {
			dev->label = iio_strdup((char *) attr->children->content);
			if (!dev->label)
				goto err_free_device;
		} else if (!strcmp((char *) attr->name, "id")) {
			dev->id = iio_strdup((char *) attr->children->content);
			if (!dev->id)
				goto err_free_device;
		} else {
			ctx_dbg(ctx, "Unknown attribute \'%s\' in <device>\n",
				attr->name);
		}
	}

	if (!dev->id) {
		ctx_err(ctx, "Unable to read device ID\n");
		err = -EINVAL;
		goto err_free_device;
	}

	for (n = n->children; n; n = n->next) {
		if (!strcmp((char *) n->name, "channel")) {
			struct iio_channel **chns,
					   *chn = create_channel(dev, n);
			err = iio_err(chn);
			if (err) {
				dev_perror(dev, err, "Unable to create channel");
				goto err_free_device;
			}

			chns = realloc(dev->channels, (1 + dev->nb_channels) *
					sizeof(struct iio_channel *));
			if (!chns) {
				err = -ENOMEM;
				dev_err(dev, "Unable to allocate memory\n");
				free(chn);
				goto err_free_device;
			}

			chns[dev->nb_channels++] = chn;
			dev->channels = chns;
		} else if (!strcmp((char *) n->name, "attribute")) {
			err = add_attr_to_device(dev, n, IIO_ATTR_TYPE_DEVICE);
			if (err < 0)
				goto err_free_device;
		} else if (!strcmp((char *) n->name, "debug-attribute")) {
			err = add_attr_to_device(dev, n, IIO_ATTR_TYPE_DEBUG);
			if (err < 0)
				goto err_free_device;
		} else if (!strcmp((char *) n->name, "buffer-attribute")) {
			err = add_attr_to_device(dev, n, IIO_ATTR_TYPE_BUFFER);
			if (err < 0)
				goto err_free_device;
		} else if (strcmp((char *) n->name, "text")) {
			dev_dbg(dev, "Unknown children \'%s\' in <device>\n",
				n->name);
			continue;
		}
	}

	return dev;

err_free_device:
	free_device(dev);

	return iio_ptr(err);
}

static const struct iio_backend_ops xml_ops = {
	.create = xml_create_context,
};

const struct iio_backend iio_xml_backend = {
	.api_version = IIO_BACKEND_API_V1,
	.name = "xml",
	.uri_prefix = "xml:",
	.ops = &xml_ops,
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

static int iio_populate_xml_context_helper(struct iio_context *ctx, xmlNode *root)
{
	xmlNode *n;
	int err;

	for (n = root->children; n; n = n->next) {
		struct iio_device *dev;

		if (!strcmp((char *) n->name, "context-attribute")) {
			err = parse_context_attr(ctx, n);
			if (err)
				return err;

			continue;
		} else if (strcmp((char *) n->name, "device")) {
			if (strcmp((char *) n->name, "text"))
				ctx_dbg(ctx, "Unknown children \'%s\' in "
					"<context>\n", n->name);
			continue;
		}

		dev = create_device(ctx, n);
		err = iio_err(dev);
		if (err) {
			ctx_perror(ctx, err, "Unable to create device");
			return err;
		}

		err = iio_context_add_device(ctx, dev);
		if (err) {
			free(dev);
			return err;
		}
	}

	return iio_context_init(ctx);
}

static struct iio_context *
iio_create_xml_context_helper(const struct iio_context_params *params,
			      xmlDoc *doc)
{
	const char *description = NULL, *git_tag = NULL, *content;
	struct iio_context *ctx;
	long major = 0, minor = 0;
	xmlNode *root;
	xmlAttr *attr;
	char *end;
	int err;

	root = xmlDocGetRootElement(doc);
	if (strcmp((char *) root->name, "context")) {
		prm_err(params, "Unrecognized XML file\n");
		return iio_ptr(-EINVAL);
	}

	for (attr = root->properties; attr; attr = attr->next) {
		content = (const char *) attr->children->content;

		if (!strcmp((char *) attr->name, "description")) {
			description = content;
		} else if (!strcmp((char *) attr->name, "version-major")) {
			errno = 0;
			major = strtol(content, &end, 10);
			if (*end != '\0' ||  errno == ERANGE)
				prm_warn(params, "invalid format for major version\n");
		} else if (!strcmp((char *) attr->name, "version-minor")) {
			errno = 0;
			minor = strtol(content, &end, 10);
			if (*end != '\0' || errno == ERANGE)
				prm_warn(params, "invalid format for minor version\n");
		} else if (!strcmp((char *) attr->name, "version-git")) {
			git_tag = content;
		} else if (strcmp((char *) attr->name, "name")) {
			prm_dbg(params, "Unknown parameter \'%s\' in <context>\n",
				content);
		}
	}

	ctx = iio_context_create_from_backend(&iio_xml_backend, description);
	err = iio_err(ctx);
	if (err) {
		prm_err(params, "Unable to allocate memory for context\n");
		return iio_ptr(err);
	}

	ctx->params = *params;

	if (git_tag) {
		ctx->major = major;
		ctx->minor = minor;

		ctx->git_tag = iio_strdup(git_tag);
		if (!ctx->git_tag) {
			iio_context_destroy(ctx);
			return iio_ptr(-ENOMEM);
		}
	}

	err = iio_populate_xml_context_helper(ctx, root);
	if (err) {
		iio_context_destroy(ctx);
		return iio_ptr(err);
	}

	return ctx;
}

static struct iio_context *
xml_create_context(const struct iio_context_params *params, const char *arg)
{
	struct iio_context *ctx;
	xmlDoc *doc;

	LIBXML_TEST_VERSION;

	if (!strncmp(arg, XML_HEADER, sizeof(XML_HEADER) - 1)) {
		doc = xmlReadMemory(arg, (int) strlen(arg),
				    NULL, NULL, XML_PARSE_DTDVALID);
	} else {
		doc = xmlReadFile(arg, NULL, XML_PARSE_DTDVALID);
	}

	if (!doc) {
		prm_err(params, "Unable to parse XML file\n");
		return iio_ptr(-EINVAL);
	}

	ctx = iio_create_xml_context_helper(params, doc);
	xmlFreeDoc(doc);
	return ctx;
}

static void cleanup_libxml2_stuff(void)
{
	/*
	 * This function will be called only when the libiio library is
	 * unloaded (e.g. when the program exits).
	 *
	 * Cleanup libxml2 so that memory analyzer tools like Valgrind won't
	 * detect a memory leak.
	 */
	xmlCleanupParser();
	xmlMemoryDump();
}

#if defined(_MSC_BUILD)
#pragma section(".CRT$XCU", read)
#define __CONSTRUCTOR(f, p) \
  static void f(void); \
  __declspec(allocate(".CRT$XCU")) void (*f##_)(void) = f; \
  __pragma(comment(linker,"/include:" p #f "_")) \
  static void f(void)
#ifdef _WIN64
#define _CONSTRUCTOR(f) __CONSTRUCTOR(f, "")
#else
#define _CONSTRUCTOR(f) __CONSTRUCTOR(f, "_")
#endif
#elif defined(__GNUC__)
#define _CONSTRUCTOR(f) static void __attribute__((constructor)) f(void)
#else
#define _CONSTRUCTOR(f) static void f(void)
#endif

_CONSTRUCTOR(initialize)
{
	/*
	 * When the library loads, register our destructor.
	 * Do it here and not in the context creation function,
	 * as it could otherwise end up registering the destructor
	 * many times.
	 */
	atexit(cleanup_libxml2_stuff);
}
#undef _CONSTRUCTOR
