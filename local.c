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
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sysfs/libsysfs.h>

#define ARRAY_SIZE(x) (sizeof(x) ? sizeof(x) / sizeof((x)[0]) : 0)

struct fn_map {
	const char *attr;
	char *filename;
};

struct iio_context_pdata {
	char *path;
};

struct iio_device_pdata {
	FILE *f;
};

struct iio_channel_pdata {
	struct fn_map *maps;
	size_t nb_maps;
};

static const char * const device_attrs_blacklist[] = {
	"dev",
	"uevent",
};

static const char * const modifier_names[] = {
	[IIO_MOD_X] = "x",
	[IIO_MOD_Y] = "y",
	[IIO_MOD_Z] = "z",
	[IIO_MOD_ROOT_SUM_SQUARED_X_Y] = "sqrt(x^2+y^2)",
	[IIO_MOD_SUM_SQUARED_X_Y_Z] = "x^2+y^2+z^2",
	[IIO_MOD_LIGHT_BOTH] = "both",
	[IIO_MOD_LIGHT_IR] = "ir",
	[IIO_MOD_LIGHT_CLEAR] = "clear",
	[IIO_MOD_LIGHT_RED] = "red",
	[IIO_MOD_LIGHT_GREEN] = "green",
	[IIO_MOD_LIGHT_BLUE] = "blue",
};

static void local_shutdown(struct iio_context *ctx)
{
	struct iio_context_pdata *pdata = ctx->pdata;
	unsigned int i;

	/* First, free the backend data stored in every device structure */
	for (i = 0; i < ctx->nb_devices; i++) {
		unsigned int j;
		struct iio_device *dev = ctx->devices[i];
		free(dev->pdata);

		/* Free backend data stored in every channel structure */
		for (j = 0; j < dev->nb_channels; j++) {
			unsigned int k;
			struct iio_channel *chn = dev->channels[j];
			struct iio_channel_pdata *ch_pdata = chn->pdata;

			for (k = 0; k < ch_pdata->nb_maps; k++)
				free(ch_pdata->maps[k].filename);
			if (ch_pdata->nb_maps)
				free(ch_pdata->maps);
			free(ch_pdata);
		}
	}

	free(pdata->path);
	free(pdata);
}

/** Shrinks the first nb characters of a string 
 * e.g. strcut("foobar", 4) replaces the content with "ar". */
static void strcut(char *str, int nb)
{
	char *ptr = str + nb;
	while (*ptr)
		*str++ = *ptr++;
	*str = 0;
}

static int set_channel_name(struct iio_channel *chn)
{
	if (chn->nb_attrs < 2)
		return 0;

	while (true) {
		bool can_fix = true;
		unsigned int i, len;
		char *name;
		const char *attr0 = chn->attrs[0];
		const char *ptr = strchr(attr0, '_');
		if (!ptr)
			break;

		len = ptr - attr0;
		for (i = 1; can_fix && i < chn->nb_attrs; i++)
			can_fix = !strncmp(attr0, chn->attrs[i], len);

		if (!can_fix)
			break;

		if (chn->name) {
			name = malloc(strlen(chn->name) + len + 2);
			if (!name)
				return -ENOMEM;
			sprintf(name, "%s_%.*s", chn->name, len, attr0);
			DEBUG("Fixing name of channel %s from %s to %s\n",
					chn->id, chn->name, name);
			free(chn->name);
		} else {
			name = malloc(len + 2);
			if (!name)
				return -ENOMEM;
			sprintf(name, "%.*s", len, attr0);
			DEBUG("Setting name of channel %s to %s\n",
					chn->id, name);
		}
		chn->name = name;

		/* Shrink the attribute name */
		for (i = 0; i < chn->nb_attrs; i++)
			strcut(chn->attrs[i], len + 1);
	}

	if (chn->name) {
		unsigned int i;
		for (i = 0; i <	ARRAY_SIZE(modifier_names); i++) {
			unsigned int len;

			if (!modifier_names[i])
				continue;

			len = strlen(modifier_names[i]);
			if (!strncmp(chn->name, modifier_names[i], len)) {
				if (chn->name[len]) {
					/* Shrink the modifier from the extended name */
					strcut(chn->name, len + 1);
				} else {
					free(chn->name);
					chn->name = NULL;
				}
				chn->modifier = i;
				DEBUG("Detected modifier for channel %s: %s\n",
						chn->id, modifier_names[i]);
				break;
			}
		}
	}
	return 0;
}

static int local_open(const struct iio_device *dev)
{
	char buf[1024];
	struct iio_device_pdata *pdata = dev->pdata;
	if (pdata->f)
		return -EAGAIN;

	sprintf(buf, "/dev/%s", dev->id);
	pdata->f = fopen(buf, "r+");
	if (!pdata->f)
		return -errno;
	return 0;
}

static int local_close(const struct iio_device *dev)
{
	struct iio_device_pdata *pdata = dev->pdata;
	int ret = fclose(pdata->f);
	if (!ret)
		pdata->f = NULL;
	return ret;
}

static ssize_t local_read(const struct iio_device *dev, void *dst, size_t len)
{
	ssize_t ret;
	FILE *f = dev->pdata->f;
	if (!f)
		return -EBADF;
	ret = fread(dst, 1, len, f);
	if (ret)
		return ret;
	else if (feof(f))
		return 0;
	else
		return -EIO;
}

static ssize_t local_write(const struct iio_device *dev,
		const void *src, size_t len)
{
	ssize_t ret;
	FILE *f = dev->pdata->f;
	if (!f)
		return -EBADF;
	ret = fwrite(src, 1, len, f);
	if (ret)
		return ret;
	else if (feof(f))
		return 0;
	else
		return -EIO;
}

static ssize_t local_read_dev_attr(const struct iio_device *dev,
		const char *attr, char *dst, size_t len)
{
	struct iio_context_pdata *pdata = dev->ctx->pdata;
	FILE *f;
	char buf[1024];
	ssize_t ret;

	sprintf(buf, "%s/devices/%s/%s", pdata->path, dev->id, attr);
	f = fopen(buf, "r");
	if (!f)
		return -errno;

	ret = fread(dst, 1, len, f);
	if (ret > 0)
		dst[ret - 1] = '\0';
	fclose(f);
	return ret;
}

static ssize_t local_write_dev_attr(const struct iio_device *dev,
		const char *attr, const char *src)
{
	struct iio_context_pdata *pdata = dev->ctx->pdata;
	FILE *f;
	char buf[1024];
	ssize_t ret;
	size_t len = strlen(src) + 1;

	sprintf(buf, "%s/devices/%s/%s", pdata->path, dev->id, attr);
	f = fopen(buf, "r+");
	if (!f)
		return -errno;

	ret = fwrite(src, 1, len, f);
	fclose(f);
	return ret;
}

static const char * get_filename(const struct iio_channel *chn,
		const char *attr)
{
	struct iio_channel_pdata *pdata = chn->pdata;
	struct fn_map *maps = pdata->maps;
	unsigned int i;
	for (i = 0; i < pdata->nb_maps; i++)
		if (!strcmp(attr, maps[i].attr))
			return maps[i].filename;
	return attr;
}

static ssize_t local_read_chn_attr(const struct iio_channel *chn,
		const char *attr, char *dst, size_t len)
{
	attr = get_filename(chn, attr);
	return local_read_dev_attr(chn->dev, attr, dst, len);
}

static ssize_t local_write_chn_attr(const struct iio_channel *chn,
		const char *attr, const char *src)
{
	attr = get_filename(chn, attr);
	return local_write_dev_attr(chn->dev, attr, src);
}

static bool is_channel(const char *attr)
{
	unsigned int i;
	char *ptr = NULL;
	if (!strncmp(attr, "in_", 3))
		ptr = strchr(attr + 3, '_');
	else if (!strncmp(attr, "out_", 4))
		ptr = strchr(attr + 4, '_');
	if (!ptr)
		return false;
	if (*(ptr - 1) >= '0' && *(ptr - 1) <= '9')
		return true;
	for (i = 0; i < ARRAY_SIZE(modifier_names); i++)
		if (modifier_names[i] && !strncmp(ptr + 1, modifier_names[i],
					strlen(modifier_names[i])))
			return true;
	return false;
}

static char * get_channel_id(const char *attr)
{
	char *res, *ptr;
	unsigned int i;

	attr = strchr(attr, '_') + 1;
	ptr = strchr(attr, '_');
	for (i = 0; i < ARRAY_SIZE(modifier_names); i++) {
		if (modifier_names[i] && !strncmp(ptr + 1, modifier_names[i],
					strlen(modifier_names[i]))) {
			ptr = strchr(ptr + 1, '_');
			break;
		}
	}

	res = malloc(ptr - attr + 1);
	if (!res)
		return NULL;

	memcpy(res, attr, ptr - attr);
	res[ptr - attr] = 0;
	return res;
}

static char * get_short_attr_name(const char *attr)
{
	char *ptr = strchr(attr, '_') + 1;
	ptr = strchr(ptr, '_') + 1;
	return strdup(ptr);
}

static int read_device_name(struct iio_device *dev)
{
	char buf[1024];
	ssize_t ret = iio_device_attr_read(dev, "name", buf, 1024);
	if (ret < 0)
		return ret;
	else if (ret == 0)
		return -EIO;

	dev->name = strdup(buf);
	if (!dev->name)
		return -ENOMEM;
	else
		return 0;
}

static int add_attr_to_device(struct iio_device *dev, const char *attr)
{
	char **attrs, *name;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(device_attrs_blacklist); i++)
		if (!strcmp(device_attrs_blacklist[i], attr))
			return 0;

	if (!strcmp(attr, "name"))
		return read_device_name(dev);

	name = strdup(attr);
	if (!name)
		return -ENOMEM;

	attrs = realloc(dev->attrs, (1 + dev->nb_attrs) * sizeof(char *));
	if (!attrs) {
		free(name);
		return -ENOMEM;
	}

	attrs[dev->nb_attrs++] = name;
	dev->attrs = attrs;
	DEBUG("Added attr \'%s\' to device \'%s\'\n", attr, dev->id);
	return 0;
}

static int add_attr_to_channel(struct iio_channel *chn, const char *attr)
{
	struct iio_channel_pdata *pdata = chn->pdata;
	struct fn_map *maps;
	char **attrs, *fn, *name = get_short_attr_name(attr);
	if (!name)
		return -ENOMEM;

	fn = strdup(attr);
	if (!fn)
		goto err_free_name;

	attrs = realloc(chn->attrs, (1 + chn->nb_attrs) * sizeof(char *));
	if (!attrs)
		goto err_free_fn;

	maps = realloc(pdata->maps,
			(1 + pdata->nb_maps) * sizeof(struct fn_map));
	if (!maps)
		goto err_update_maps;

	maps[pdata->nb_maps].attr = name;
	maps[pdata->nb_maps++].filename = fn;
	pdata->maps = maps;

	attrs[chn->nb_attrs++] = name;
	chn->attrs = attrs;
	DEBUG("Added attr \'%s\' to channel \'%s\'\n", name, chn->id);
	return 0;

err_update_maps:
	/* the first realloc succeeded so we must update chn->attrs
	 * even if an error occured later */
	chn->attrs = attrs;
err_free_fn:
	free(fn);
err_free_name:
	free(name);
	return -ENOMEM;
}

static int add_channel_to_device(struct iio_device *dev,
		struct iio_channel *chn)
{
	struct iio_channel **channels = realloc(dev->channels,
			(dev->nb_channels + 1) * sizeof(struct iio_channel *));
	if (!channels)
		return -ENOMEM;

	channels[dev->nb_channels++] = chn;
	dev->channels = channels;
	DEBUG("Added channel \'%s\' to device \'%s\'\n", chn->id, dev->id);
	return 0;
}

static int add_device_to_context(struct iio_context *ctx,
		struct iio_device *dev)
{
	struct iio_device **devices = realloc(ctx->devices,
			(ctx->nb_devices + 1) * sizeof(struct iio_device *));
	if (!devices)
		return -ENOMEM;

	devices[ctx->nb_devices++] = dev;
	ctx->devices = devices;
	DEBUG("Added device \'%s\' to context \'%s\'\n", dev->id, ctx->name);
	return 0;
}

static bool is_global_attr(struct iio_channel *chn, const char *attr)
{
	unsigned int i, len;
	char *ptr;

	if (!chn->is_output && !strncmp(attr, "in_", 3))
		attr += 3;
	else if (chn->is_output && !strncmp(attr, "out_", 4))
		attr += 4;
	else
		return false;

	ptr = strchr(attr, '_');
	if (!ptr)
		return false;

	len = ptr - attr;

	if (strncmp(chn->id, attr, len))
		return false;

	DEBUG("Found match: %s and %s\n", chn->id, attr);
	if (chn->id[len] >= '0' && chn->id[len] <= '9')
		return true;
	else if (chn->id[len] != '_')
		return false;

	for (i = 0; i < ARRAY_SIZE(modifier_names); i++)
		if (modifier_names[i] &&
				!strncmp(chn->id + len + 1, modifier_names[i],
					strlen(modifier_names[i])))
			return true;

	return false;
}

static int detect_and_move_global_attrs(struct iio_device *dev)
{
	unsigned int i;
	char **ptr = dev->attrs;

	for (i = 0; i < dev->nb_attrs; i++) {
		unsigned int j;
		bool global = false;
		const char *attr = dev->attrs[i];

		for (j = 0; j < dev->nb_channels; j++) {
			struct iio_channel *chn = dev->channels[j];
			if (is_global_attr(chn, attr)) {
				int ret;
				global = true;
				ret = add_attr_to_channel(chn, attr);
				if (ret)
					return ret;
			}
		}

		if (global) {
			free(dev->attrs[i]);
			dev->attrs[i] = NULL;
		}
	}

	for (i = 0; i < dev->nb_attrs; i++) {
		if (dev->attrs[i])
			*ptr++ = dev->attrs[i];
	}

	dev->nb_attrs = ptr - dev->attrs;
	return 0;
}

static struct iio_channel *create_channel(struct iio_device *dev,
		char *id, const char *attr)
{
	struct iio_channel *chn = calloc(1, sizeof(*chn));
	if (!chn)
		return NULL;

	chn->pdata = calloc(1, sizeof(*chn->pdata));
	if (!dev->pdata)
		goto err_free_chn;

	if (!strncmp(attr, "out_", 4))
		chn->is_output = true;
	else if (strncmp(attr, "in_", 3))
		goto err_free_pdata;

	chn->dev = dev;
	chn->id = id;
	chn->modifier = IIO_NO_MOD;

	if (!add_attr_to_channel(chn, attr))
		return chn;

err_free_pdata:
	free(chn->pdata);
err_free_chn:
	free(chn);
	return NULL;
}

static struct iio_device *create_device(struct iio_context *ctx,
		struct sysfs_device *device)
{
	struct iio_device *dev;
	struct dlist *attrlist;
	struct sysfs_attribute *attr;
	unsigned int i;

	dev = calloc(1, sizeof(*dev));
	if (!dev)
		return NULL;

	dev->pdata = calloc(1, sizeof(*dev->pdata));
	if (!dev->pdata) {
		free(dev);
		return NULL;
	}

	dev->ctx = ctx;
	dev->id = strdup(device->name);
	if (!dev->id) {
		free(dev->pdata);
		free(dev);
		return NULL;
	}

	attrlist = sysfs_get_device_attributes(device);
	dlist_for_each_data(attrlist, attr, struct sysfs_attribute) {
		if (is_channel(attr->name)) {
			unsigned int i;
			bool new_channel = true;
			struct iio_channel *chn;
			char *channel_id = get_channel_id(attr->name);
			if (!channel_id) {
				free_device(dev);
				return NULL;
			}

			for (i = 0; i < dev->nb_channels; i++) {
				chn = dev->channels[i];
				if (strcmp(chn->id, channel_id))
					continue;

				free(channel_id);
				new_channel = false;
				if (add_attr_to_channel(chn, attr->name)) {
					free_device(dev);
					return NULL;
				}
				break;
			}

			if (!new_channel)
				continue;

			chn = create_channel(dev, channel_id, attr->name);
			if (!chn) {
				free(channel_id);
				free_device(dev);
				return NULL;
			}
			if (add_channel_to_device(dev, chn)) {
				free_channel(chn);
				free_device(dev);
				return NULL;
			}
		} else if (add_attr_to_device(dev, attr->name)) {
			free_device(dev);
			return NULL;
		}
	}

	for (i = 0; i < dev->nb_channels; i++)
		set_channel_name(dev->channels[i]);

	if (detect_and_move_global_attrs(dev)) {
		free_device(dev);
		return NULL;
	}

	return dev;
}

static struct iio_backend_ops local_ops = {
	.open = local_open,
	.close = local_close,
	.read = local_read,
	.write = local_write,
	.read_device_attr = local_read_dev_attr,
	.write_device_attr = local_write_dev_attr,
	.read_channel_attr = local_read_chn_attr,
	.write_channel_attr = local_write_chn_attr,
	.shutdown = local_shutdown,
};

struct iio_context * iio_create_local_context(void)
{
	struct sysfs_bus *iio;
	struct sysfs_device *device;
	struct dlist *devlist;
	char *path;

	struct iio_context *ctx = calloc(1, sizeof(*ctx));
	if (!ctx) {
		ERROR("Unable to allocate memory\n");
		return NULL;
	}

	ctx->pdata = calloc(1, sizeof(*ctx->pdata));
	if (!ctx->pdata) {
		ERROR("Unable to allocate memory\n");
		free(ctx);
		return NULL;
	}

	ctx->ops = &local_ops;
	ctx->name = "local";

	iio = sysfs_open_bus("iio");
	if (!iio) {
		ERROR("Unable to open IIO bus\n");
		goto err_destroy_ctx;
	}

	path = strdup(iio->path);
	if (!path) {
		ERROR("Unable to allocate memory\n");
		goto err_close_sysfs_bus;
	}

	ctx->pdata->path = path;

	devlist = sysfs_get_bus_devices(iio);
	dlist_for_each_data(devlist, device, struct sysfs_device) {
		struct iio_device *dev = create_device(ctx, device);
		if (!dev) {
			ERROR("Unable to create IIO device structure\n");
			goto err_close_sysfs_bus;
		}

		if (add_device_to_context(ctx, dev)) {
			ERROR("Unable to allocate memory\n");
			free(dev);
			goto err_close_sysfs_bus;
		}
	}

	sysfs_close_bus(iio);

	return ctx;

err_close_sysfs_bus:
	sysfs_close_bus(iio);
err_destroy_ctx:
	iio_context_destroy(ctx);
	return NULL;
}
