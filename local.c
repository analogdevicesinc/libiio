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

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define ARRAY_SIZE(x) (sizeof(x) ? sizeof(x) / sizeof((x)[0]) : 0)

struct fn_map {
	const char *attr;
	char *filename;
};

struct iio_device_pdata {
	FILE *f;
	unsigned int samples_count;
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

static ssize_t local_read(const struct iio_device *dev,
		void *dst, size_t len, uint32_t *mask, size_t words)
{
	ssize_t ret;
	FILE *f = dev->pdata->f;
	if (!f)
		return -EBADF;
	if (words != dev->words)
		return -EINVAL;

	memcpy(mask, dev->mask, words);
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
	FILE *f;
	char buf[1024];
	ssize_t ret;

	snprintf(buf, sizeof(buf), "/sys/bus/iio/devices/%s/%s", dev->id, attr);
	f = fopen(buf, "r");
	if (!f)
		return -errno;

	ret = fread(dst, 1, len, f);
	if (ret > 0)
		dst[ret - 1] = '\0';
	fflush(f);
	if (ferror(f))
		ret = -EBUSY;
	fclose(f);
	return ret ?: -EIO;
}

static ssize_t local_write_dev_attr(const struct iio_device *dev,
		const char *attr, const char *src)
{
	FILE *f;
	char buf[1024];
	ssize_t ret;
	size_t len = strlen(src) + 1;

	snprintf(buf, sizeof(buf), "/sys/bus/iio/devices/%s/%s", dev->id, attr);
	f = fopen(buf, "r+");
	if (!f)
		return -errno;

	ret = fwrite(src, 1, len, f);
	fflush(f);
	if (ferror(f))
		ret = -EBUSY;
	fclose(f);
	return ret ?: -EIO;
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

static int channel_write_state(const struct iio_channel *chn)
{
	char *en = iio_channel_is_enabled(chn) ? "1" : "0";
	ssize_t ret = local_write_chn_attr(chn, "en", en);
	if (ret < 0)
		return (int) ret;
	else
		return 0;
}

static int local_open(const struct iio_device *dev,
		size_t samples_count, uint32_t *mask, size_t nb)
{
	unsigned int i;
	int ret;
	char buf[1024];
	struct iio_device_pdata *pdata = dev->pdata;
	if (pdata->f)
		return -EBUSY;

	if (nb != dev->words)
		return -EINVAL;

	ret = local_write_dev_attr(dev, "buffer/enable", "0");
	if (ret < 0)
		return ret;

	if (samples_count) {
		sprintf(buf, "%lu", (unsigned long) samples_count);
		ret = local_write_dev_attr(dev, "buffer/length", buf);
		if (ret < 0)
			return ret;
	}

	sprintf(buf, "/dev/%s", dev->id);
	pdata->f = fopen(buf, "r+");
	if (!pdata->f)
		return -errno;

	memcpy(dev->mask, mask, nb);

	/* Enable channels */
	for (i = 0; i < dev->nb_channels; i++) {
		struct iio_channel *chn = dev->channels[i];
		if (chn->index >= 0) {
			ret = channel_write_state(chn);
			if (ret < 0)
				goto err_close;
		}
	}

	pdata->samples_count = samples_count;

	/* If opened with samples_count == 0, we probably want DDS mode;
	 * then the buffer will only be enabled when closing the device. */
	if (samples_count > 0)
		ret = local_write_dev_attr(dev, "buffer/enable", "1");
	if (ret < 0)
		goto err_close;

	return 0;
err_close:
	fclose(pdata->f);
	pdata->f = NULL;
	return ret;
}

static int local_close(const struct iio_device *dev)
{
	struct iio_device_pdata *pdata = dev->pdata;
	int ret;

	if (!pdata->f)
		return -EBADF;

	ret = fclose(pdata->f);
	if (ret)
		return ret;

	pdata->f = NULL;
	ret = local_write_dev_attr(dev, "buffer/enable",
			pdata->samples_count ? "0" : "1");
	if (ret < 0)
		return ret;
	else
		return 0;
}

static int local_get_trigger(const struct iio_device *dev,
		const struct iio_device **trigger)
{
	char buf[1024];
	unsigned int i;
	ssize_t nb = local_read_dev_attr(dev, "trigger/current_trigger",
			buf, sizeof(buf));
	if (nb < 0) {
		*trigger = NULL;
		return (int) nb;
	}

	nb = dev->ctx->nb_devices;
	for (i = 0; i < nb; i++) {
		const struct iio_device *cur = dev->ctx->devices[i];
		if (cur->name && !strcmp(cur->name, buf)) {
			*trigger = cur;
			return 0;
		}
	}
	return -ENXIO;
}

static int local_set_trigger(const struct iio_device *dev,
		const struct iio_device *trigger)
{
	ssize_t nb;
	const char *value = trigger ? trigger->name : "";
	nb = local_write_dev_attr(dev, "trigger/current_trigger", value);
	if (nb < 0)
		return (int) nb;
	else
		return 0;
}

static bool is_channel(const char *attr)
{
	unsigned int i;
	char *ptr = NULL;
	if (!strncmp(attr, "in_timestamp_", sizeof("in_timestamp_") - 1))
		return true;
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

static int add_attr_to_channel(struct iio_channel *chn,
		const char *attr, const char *path)
{
	struct iio_channel_pdata *pdata = chn->pdata;
	struct fn_map *maps;
	char **attrs, *fn, *name = get_short_attr_name(attr);
	if (!name)
		return -ENOMEM;

	fn = strdup(path);
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
				ret = add_attr_to_channel(chn, attr, attr);
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
		char *id, const char *attr, const char *path)
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

	if (!add_attr_to_channel(chn, attr, path))
		return chn;

err_free_pdata:
	free(chn->pdata);
err_free_chn:
	free(chn);
	return NULL;
}

static int add_attr_or_channel_helper(struct iio_device *dev,
		const char *path, const char *prefix)
{
	int ret;
	unsigned int i;
	struct iio_channel *chn;
	char buf[1024], *channel_id;
	const char *name = strrchr(path, '/') + 1;
	if (!prefix) {
		path = name;
	} else {
		snprintf(buf, sizeof(buf), "%s/%s", prefix, name);
		path = buf;
	}

	if (!is_channel(name))
		return add_attr_to_device(dev, name);

	channel_id = get_channel_id(name);
	if (!channel_id)
		return -ENOMEM;

	for (i = 0; i < dev->nb_channels; i++) {
		chn = dev->channels[i];
		if (!strcmp(chn->id, channel_id)
				&& chn->is_output == (name[0] == 'o')) {
			free(channel_id);
			return add_attr_to_channel(chn, name, path);
		}
	}

	chn = create_channel(dev, channel_id, name, path);
	if (!chn) {
		free(channel_id);
		return -ENXIO;
	}
	ret = add_channel_to_device(dev, chn);
	if (ret)
		free_channel(chn);
	return ret;
}

static int add_attr_or_channel(void *d, const char *path)
{
	return add_attr_or_channel_helper((struct iio_device *) d, path, NULL);
}

static int add_scan_element(void *d, const char *path)
{
	return add_attr_or_channel_helper((struct iio_device *) d,
			path, "scan_elements");
}

static int foreach_in_dir(void *d, const char *path, bool is_dir,
		int (*callback)(void *, const char *))
{
	long name_max;
	struct dirent *entry, *result;
	DIR *dir = opendir(path);
	if (!dir)
		return -errno;

	name_max = pathconf(path, _PC_NAME_MAX);
	if (name_max == -1)
		name_max = 255;
	entry = malloc(offsetof(struct dirent, d_name) + name_max + 1);
	if (!entry) {
		closedir(dir);
		return -ENOMEM;
	}

	while (true) {
		struct stat st;
		char buf[1024];
		int ret = readdir_r(dir, entry, &result);
		if (ret) {
			strerror_r(ret, buf, sizeof(buf));
			ERROR("Unable to open directory %s: %s\n", path, buf);
			free(entry);
			closedir(dir);
			return ret;
		}
		if (!result)
			break;

		snprintf(buf, sizeof(buf), "%s/%s", path, entry->d_name);
		if (stat(buf, &st) < 0) {
			ret = -errno;
			strerror_r(errno, buf, sizeof(buf));
			ERROR("Unable to stat file: %s\n", buf);
			free(entry);
			closedir(dir);
			return ret;
		}

		if (is_dir && S_ISDIR(st.st_mode) && entry->d_name[0] != '.')
			ret = callback(d, buf);
		else if (!is_dir && S_ISREG(st.st_mode))
			ret = callback(d, buf);
		else
			continue;

		if (ret < 0) {
			free(entry);
			closedir(dir);
			return ret;
		}
	}

	free(entry);
	closedir(dir);
	return 0;
}

static int add_scan_elements(struct iio_device *dev, const char *devpath)
{
	struct stat st;
	char buf[1024];
	snprintf(buf, sizeof(buf), "%s/scan_elements", devpath);

	if (!stat(buf, &st) && S_ISDIR(st.st_mode)) {
		int ret = foreach_in_dir(dev, buf, false, add_scan_element);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int create_device(void *d, const char *path)
{
	uint32_t *mask = NULL;
	unsigned int i;
	int ret;
	struct iio_context *ctx = d;
	struct iio_device *dev = calloc(1, sizeof(*dev));
	if (!dev)
		return -ENOMEM;

	dev->pdata = calloc(1, sizeof(*dev->pdata));
	if (!dev->pdata) {
		free(dev);
		return -ENOMEM;
	}

	dev->ctx = ctx;
	dev->id = strdup(strrchr(path, '/') + 1);
	if (!dev->id) {
		free(dev->pdata);
		free(dev);
		return -ENOMEM;
	}

	ret = foreach_in_dir(dev, path, false, add_attr_or_channel);
	if (ret < 0) {
		free_device(dev);
		return ret;
	}

	for (i = 0; i < dev->nb_channels; i++)
		set_channel_name(dev->channels[i]);

	ret = detect_and_move_global_attrs(dev);
	if (ret < 0) {
		free_device(dev);
		return ret;
	}

	ret = add_scan_elements(dev, path);
	if (ret < 0) {
		free_device(dev);
		return ret;
	}

	dev->words = (dev->nb_channels + 31) / 32;
	if (dev->words) {
		mask = calloc(dev->words, sizeof(*mask));
		if (!mask) {
			free_device(dev);
			return ret;
		}
	}

	dev->mask = mask;

	ret = add_device_to_context(ctx, dev);
	if (ret < 0)
		free_device(dev);
	return ret;
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
	.get_trigger = local_get_trigger,
	.set_trigger = local_set_trigger,
	.shutdown = local_shutdown,
};

struct iio_context * iio_create_local_context(void)
{
	int ret;
	struct iio_context *ctx = calloc(1, sizeof(*ctx));
	if (!ctx) {
		ERROR("Unable to allocate memory\n");
		return NULL;
	}

	ctx->ops = &local_ops;
	ctx->name = "local";

	ret = foreach_in_dir(ctx, "/sys/bus/iio/devices", true, create_device);
	if (ret < 0) {
		char buf[1024];
		strerror_r(errno, buf, sizeof(buf));
		ERROR("Unable to create context: %s\n", buf);
		iio_context_destroy(ctx);
		ctx = NULL;
	}

	iio_context_init_channels(ctx);
	return ctx;
}
