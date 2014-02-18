#include "debug.h"
#include "iio-private.h"

#include <errno.h>
#include <libxml/tree.h>
#include <string.h>

struct value_map {
	const struct iio_device *dev;
	const struct iio_channel *chn;
	const char *attr;
	char *value;
};

struct xml_pdata {
	struct value_map *values;
	unsigned int nb_values;
};

static int add_attr_to_channel(struct iio_channel *chn, xmlNode *n)
{
	xmlAttr *attr;
	char *name = NULL, *value = NULL;
	const char **attrs;
	struct xml_pdata *pdata = chn->dev->ctx->backend_data;
	struct value_map *values;

	for (attr = n->properties; attr; attr = attr->next) {
		if (!strcmp((char *) attr->name, "name")) {
			name = strdup((char *) attr->children->content);
		} else if (!strcmp((char *) attr->name, "value")) {
			value = strdup((char *) attr->children->content);
		} else {
			WARNING("Unknown field \'%s\' in channel %s\n",
					attr->name, chn->id);
		}
	}

	if (!name || !value) {
		ERROR("Incomplete attribute in channel %s\n", chn->id);
		goto err_free;
	}

	attrs = realloc(chn->attrs, (1 + chn->nb_attrs) * sizeof(char *));
	if (!attrs)
		goto err_free;

	values = realloc(pdata->values,
			(1 + pdata->nb_values) * sizeof(struct value_map));
	if (!values)
		goto err_update_attrs;

	values[pdata->nb_values].dev = NULL;
	values[pdata->nb_values].chn = chn;
	values[pdata->nb_values].attr = name;
	values[pdata->nb_values++].value = value;
	pdata->values = values;

	attrs[chn->nb_attrs++] = name;
	chn->attrs = attrs;
	return 0;

err_update_attrs:
	/* the first realloc succeeded so we must update chn->attrs
	 * even if an error occured later */
	chn->attrs = attrs;
err_free:
	if (name)
		free(name);
	if (value)
		free(value);
	return -1;
}

static int add_attr_to_device(struct iio_device *dev, xmlNode *n)
{
	xmlAttr *attr;
	char *name = NULL, *value = NULL;
	const char **attrs;
	struct xml_pdata *pdata = dev->ctx->backend_data;
	struct value_map *values;

	for (attr = n->properties; attr; attr = attr->next) {
		if (!strcmp((char *) attr->name, "name")) {
			name = strdup((char *) attr->children->content);
		} else if (!strcmp((char *) attr->name, "value")) {
			value = strdup((char *) attr->children->content);
		} else {
			WARNING("Unknown field \'%s\' in device %s\n",
					attr->name, dev->id);
		}
	}

	if (!name || !value) {
		ERROR("Incomplete attribute in device %s\n", dev->id);
		goto err_free;
	}

	attrs = realloc(dev->attrs, (1 + dev->nb_attrs) * sizeof(char *));
	if (!attrs)
		goto err_free;

	values = realloc(pdata->values,
			(1 + pdata->nb_values) * sizeof(struct value_map));
	if (!values)
		goto err_update_attrs;

	values[pdata->nb_values].dev = dev;
	values[pdata->nb_values].chn = NULL;
	values[pdata->nb_values].attr = name;
	values[pdata->nb_values++].value = value;
	pdata->values = values;

	attrs[dev->nb_attrs++] = name;
	dev->attrs = attrs;
	return 0;

err_update_attrs:
	/* the first realloc succeeded so we must update dev->attrs
	 * even if an error occured later */
	dev->attrs = attrs;
err_free:
	if (name)
		free(name);
	if (value)
		free(value);
	return -1;
}

static struct iio_channel * create_channel(struct iio_device *dev, xmlNode *n)
{
	xmlAttr *attr;
	struct iio_channel *chn = calloc(1, sizeof(*chn));
	if (!chn)
		return NULL;

	chn->dev = dev;
	chn->type = IIO_CHANNEL_UNKNOWN;

	for (attr = n->properties; attr; attr = attr->next) {
		if (!strcmp((char *) attr->name, "name")) {
			chn->name = strdup((char *) attr->children->content);
		} else if (!strcmp((char *) attr->name, "id")) {
			chn->id = strdup((char *) attr->children->content);
		} else if (!strcmp((char *) attr->name, "type")) {
			if (!strcmp((char *) attr->children->content, "input"))
				chn->type = IIO_CHANNEL_INPUT;
			else if (!strcmp((char *) attr->children->content,
						"output")) {
				chn->type = IIO_CHANNEL_OUTPUT;
			} else {
				WARNING("Unknown channel type %s\n", (char *)
						attr->children->content);
			}
		} else {
			WARNING("Unknown attribute \'%s\' in <channel>\n",
					attr->name);
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
		} else if (strcmp((char *) n->name, "text")) {
			WARNING("Unknown children \'%s\' in <device>\n",
					n->name);
			continue;
		}
	}

	return chn;

err_free_channel:
	free_channel(chn);
	return NULL;
}

static struct iio_device * create_device(struct iio_context *ctx, xmlNode *n)
{
	xmlAttr *attr;
	struct iio_device *dev = calloc(1, sizeof(*dev));
	if (!dev)
		return NULL;

	dev->ctx = ctx;

	for (attr = n->properties; attr; attr = attr->next) {
		if (!strcmp((char *) attr->name, "name")) {
			dev->name = strdup((char *) attr->children->content);
		} else if (!strcmp((char *) attr->name, "id")) {
			dev->id = strdup((char *) attr->children->content);
		} else {
			WARNING("Unknown attribute \'%s\' in <context>\n",
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
			if (add_attr_to_device(dev, n) < 0)
				goto err_free_device;
		} else if (strcmp((char *) n->name, "text")) {
			WARNING("Unknown children \'%s\' in <device>\n",
					n->name);
			continue;
		}
	}

	return dev;

err_free_device:
	free_device(dev);
	return NULL;
}

static ssize_t xml_read_attr_helper(struct xml_pdata *pdata,
		const struct iio_device *dev,
		const struct iio_channel *chn,
		const char *path, char *dst, size_t len)
{
	unsigned int i;
	for (i = 0; i < pdata->nb_values; i++) {
		struct value_map *map = &pdata->values[i];

		if (dev == map->dev && chn == map->chn
				&& !strcmp(path, map->attr)) {
			size_t value_len = strlen(map->value);
			strncpy(dst, map->value, len);
			return value_len + 1;
		}
	}

	return -ENOENT;
}

static ssize_t xml_read_dev_attr(const struct iio_device *dev,
		const char *path, char *dst, size_t len)
{
	struct xml_pdata *pdata = dev->ctx->backend_data;
	return xml_read_attr_helper(pdata, dev, NULL, path, dst, len);
}

static ssize_t xml_read_chn_attr(const struct iio_channel *chn,
		const char *path, char *dst, size_t len)
{
	struct xml_pdata *pdata = chn->dev->ctx->backend_data;
	return xml_read_attr_helper(pdata, NULL, chn, path, dst, len);
}

static void xml_shutdown(struct iio_context *ctx)
{
	struct xml_pdata *pdata = ctx->backend_data;
	unsigned int i;
	for (i = 0; i < pdata->nb_values; i++) {
		struct value_map *map = &pdata->values[i];

		/* note: map->attr and map->dev are freed elsewhere */
		free(map->value);
	}

	if (pdata->nb_values)
		free(pdata->values);
	free(pdata);
}

static struct iio_backend_ops xml_ops = {
	.read_device_attr = xml_read_dev_attr,
	.read_channel_attr = xml_read_chn_attr,
	.shutdown = xml_shutdown,
};

struct iio_context * iio_create_xml_context(const char *xml_file)
{
	unsigned int i;
	xmlDoc *doc;
	xmlNode *root, *n;
	struct iio_context *ctx = calloc(1, sizeof(*ctx));
	if (!ctx)
		return NULL;

	ctx->backend_data = calloc(1, sizeof(struct xml_pdata));
	if (!ctx->backend_data) {
		ERROR("Unable to allocate memory\n");
		free(ctx);
		return NULL;
	}

	ctx->name = "xml";
	ctx->ops = &xml_ops;

	LIBXML_TEST_VERSION;

	doc = xmlReadFile(xml_file, NULL, 0);
	if (!doc) {
		ERROR("Unable to parse XML file\n");
		goto err_free_ctx;
	}

	root = xmlDocGetRootElement(doc);
	if (strcmp((char *) root->name, "context")) {
		ERROR("Unrecognized XML file\n");
		goto err_free_doc;
	}

	for (n = root->children; n; n = n->next) {
		struct iio_device **devs, *dev;

		if (strcmp((char *) n->name, "device")) {
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

	return ctx;

err_free_devices:
	for (i = 0; i < ctx->nb_devices; i++)
		free_device(ctx->devices[i]);
	if (ctx->nb_devices)
		free(ctx->devices);
err_free_doc:
	xmlFreeDoc(doc);
	xmlCleanupParser();
err_free_ctx:
	free(ctx);
	return NULL;
}
