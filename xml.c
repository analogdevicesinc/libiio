// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include <errno.h>
#include <iio/iio-backend.h>
#include <iio/iio-debug.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <string.h>

#define XML_HEADER "<?xml version=\"1.0\""

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
			chn_dbg(chn, "Unknown field \'%s\'\n", attr->name);
		}
	}

	if (!name) {
		chn_err(chn, "Incomplete attribute\n");
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
			dev_dbg(dev, "Unknown field \'%s\'\n", attr->name);
		}
	}

	if (!name) {
		dev_err(dev, "Incomplete attribute\n");
		return -EINVAL;
	}

	return iio_device_add_attr(dev, name, type);
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
		dev_err(dev, "Incomplete <attribute>\n");
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

	/* Those have been duplicated into the iio_device. */
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
		} else if (!strcmp((char *) n->name, "attribute")) {
			err = add_attr_to_device(dev, n, IIO_ATTR_TYPE_DEVICE);
			if (err < 0)
				return err;
		} else if (!strcmp((char *) n->name, "debug-attribute")) {
			err = add_attr_to_device(dev, n, IIO_ATTR_TYPE_DEBUG);
			if (err < 0)
				return err;
		} else if (!strcmp((char *) n->name, "buffer-attribute")) {
			err = add_attr_to_device(dev, n, IIO_ATTR_TYPE_BUFFER);
			if (err < 0)
				return err;
		} else if (strcmp((char *) n->name, "text")) {
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

		err = create_device(ctx, n);
		if (err) {
			ctx_perror(ctx, err, "Unable to create device");
			return err;
		}
	}

	return 0;
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

	ctx = iio_context_create_from_backend(params, &iio_xml_backend,
					      description,
					      major, minor, git_tag);
	err = iio_err(ctx);
	if (err) {
		prm_err(params, "Unable to allocate memory for context\n");
		return iio_ptr(err);
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

void libiio_cleanup_xml_backend(void)
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
