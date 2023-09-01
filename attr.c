// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2023 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "iio-private.h"

#include <inttypes.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static inline unsigned int attr_index(const struct iio_attr_list *list,
				      const struct iio_attr *attr)
{
	uintptr_t diff = (uintptr_t)attr - (uintptr_t)list->attrs;
	return (unsigned int)diff / sizeof(*attr);
}

ssize_t iio_attr_read_raw(const struct iio_attr *attr, char *dst, size_t len)
{
	const struct iio_device *dev = iio_attr_get_device(attr);
	unsigned int idx;

	if (attr->type == IIO_ATTR_TYPE_CONTEXT) {
		idx = attr_index(&attr->iio.ctx->attrlist, attr);
		return (ssize_t) iio_strlcpy(dst, attr->iio.ctx->values[idx], len);
	}

	if (dev->ctx->ops->read_attr)
		return dev->ctx->ops->read_attr(attr, dst, len);

	return -ENOSYS;
}

int iio_attr_read_longlong(const struct iio_attr *attr, long long *val)
{
	char *end, buf[1024];
	long long value;
	ssize_t ret;

	ret = iio_attr_read_raw(attr, buf, sizeof(buf));
	if (ret < 0)
		return (int) ret;

	errno = 0;
	value = strtoll(buf, &end, 0);
	if (end == buf || errno == ERANGE)
		return -EINVAL;
	*val = value;
	return 0;
}

int iio_attr_read_bool(const struct iio_attr *attr, bool *val)
{
	long long value;
	int ret;

	ret = iio_attr_read_longlong(attr, &value);
	if (ret < 0)
		return ret;

	*val = !!value;
	return 0;
}

int iio_attr_read_double(const struct iio_attr *attr, double *val)
{
	char buf[1024];
	ssize_t ret;

	ret = iio_attr_read_raw(attr, buf, sizeof(buf));
	if (ret < 0)
		return (int) ret;

	return read_double(buf, val);
}

ssize_t iio_attr_write_raw(const struct iio_attr *attr,
			   const void *src, size_t len)
{
	const struct iio_device *dev = iio_attr_get_device(attr);

	if (attr->type == IIO_ATTR_TYPE_CONTEXT)
		return -EPERM;

	if (dev->ctx->ops->write_attr)
		return dev->ctx->ops->write_attr(attr, src, len);

	return -ENOSYS;
}

ssize_t iio_attr_write_string(const struct iio_attr *attr, const char *src)
{
	return iio_attr_write_raw(attr, src, strlen(src) + 1); /* Flawfinder: ignore */
}

int iio_attr_write_longlong(const struct iio_attr *attr, long long val)
{
	size_t len;
	ssize_t ret;
	char buf[1024];

	len = iio_snprintf(buf, sizeof(buf), "%lld", val);
	ret = iio_attr_write_raw(attr, buf, len + 1);

	return (int) (ret < 0 ? ret : 0);
}

int iio_attr_write_double(const struct iio_attr *attr, double val)
{
	ssize_t ret;
	char buf[1024];

	ret = (ssize_t) write_double(buf, sizeof(buf), val);
	if (!ret)
		ret = iio_attr_write_string(attr, buf);
	return (int) (ret < 0 ? ret : 0);
}

int iio_attr_write_bool(const struct iio_attr *attr, bool val)
{
	ssize_t ret;

	if (val)
		ret = iio_attr_write_raw(attr, "1", 2);
	else
		ret = iio_attr_write_raw(attr, "0", 2);

	return (int) (ret < 0 ? ret : 0);
}

const struct iio_attr *
iio_attr_get(const struct iio_attr_list *attrs, unsigned int idx)
{
	if (idx >= attrs->num)
		return NULL;

	return &attrs->attrs[idx];
}

const struct iio_attr *
iio_attr_find(const struct iio_attr_list *attrs, const char *name)
{
	unsigned int i;

	for (i = 0; i < attrs->num; i++)
		if (!strcmp(attrs->attrs[i].name, name))
			return iio_attr_get(attrs, i);

	return NULL;
}

const char *
iio_attr_get_name(const struct iio_attr *attr)
{
	return attr->name;
}

const char *
iio_attr_get_filename(const struct iio_attr *attr)
{
	return attr->filename;
}

const char *
iio_attr_get_static_value(const struct iio_attr *attr)
{
	unsigned int idx;

	switch (attr->type) {
	case IIO_ATTR_TYPE_CONTEXT:
		idx = attr_index(&attr->iio.ctx->attrlist, attr);
		return attr->iio.ctx->values[idx];
	default:
		return NULL;
	}
}

int iio_add_attr(union iio_pointer p, struct iio_attr_list *attrs,
		 const char *name, const char *filename,
		 enum iio_attr_type type)
{
	struct iio_attr *attr;

	attr = realloc(attrs->attrs, (1 + attrs->num) * sizeof(*attrs->attrs));
	if (!attr)
		return -ENOMEM;

	attrs->attrs = attr;
	attr[attrs->num] = (struct iio_attr){
		.iio = p,
		.type = type,
		.name = iio_strdup(name),
		.filename = NULL,
	};

	if (!attr[attrs->num].name)
		return -ENOMEM;

	if (filename) {
		attr[attrs->num].filename = iio_strdup(filename);

		if (!attr[attrs->num].filename) {
			free((char *) attr[attrs->num].name);
			return -ENOMEM;
		}
	} else {
		attr[attrs->num].filename = attr[attrs->num].name;
	}

	attrs->num++;

	return 0;
}

static const char * const attr_type_string[] = {
	"",
	" debug",
	" buffer",
};

int iio_device_add_attr(struct iio_device *dev,
			const char *name, enum iio_attr_type type)
{
	union iio_pointer p = { .dev = dev, };
	int ret;

	ret = iio_add_attr(p, &dev->attrlist[type], name, NULL, type);
	if (ret < 0)
		return ret;

	dev_dbg(dev, "Added%s attr \'%s\' to device \'%s\'\n",
		attr_type_string[type], name, dev->id);
	return 0;
}

int iio_channel_add_attr(struct iio_channel *chn,
			 const char *name, const char *filename)
{
	union iio_pointer p = { .chn = chn, };
	int ret;

	ret = iio_add_attr(p, &chn->attrlist, name, filename,
			   IIO_ATTR_TYPE_CHANNEL);
	if (ret < 0)
		return ret;

	chn_dbg(chn, "Added attr \'%s\' (\'%s\') to channel \'%s\'\n",
		name, filename, chn->id);
	return 0;
}

int iio_context_add_attr(struct iio_context *ctx,
			 const char *key, const char *value)
{
	char **values, *new_val;
	union iio_pointer p = { .ctx = ctx, };
	unsigned int i;
	int ret;

	new_val = iio_strdup(value);
	if (!new_val)
		return -ENOMEM;

	for (i = 0; i < ctx->attrlist.num; i++) {
		if(!strcmp(ctx->attrlist.attrs[i].name, key)) {
			free(ctx->values[i]);
			ctx->values[i] = new_val;
			return 0;
		}
	}

	values = realloc(ctx->values,
			(ctx->attrlist.num + 1) * sizeof(*ctx->values));
	if (!values) {
		free(new_val);
		return -ENOMEM;
	}

	values[ctx->attrlist.num] = new_val;
	ctx->values = values;

	ret = iio_add_attr(p, &ctx->attrlist, key, NULL, IIO_ATTR_TYPE_CONTEXT);
	if (ret) {
		free(new_val);
		return ret;
	}

	return 0;
}

void iio_free_attr_data(struct iio_attr *attr)
{
	if (attr->filename != attr->name)
		free((char *) attr->filename);

	free((char *) attr->name);

	attr->filename = NULL;
	attr->name = NULL;
}

void iio_free_attrs(const struct iio_attr_list *attrs)
{
	unsigned int i;

	for (i = 0; i < attrs->num; i++)
		iio_free_attr_data(&attrs->attrs[i]);

	free(attrs->attrs);
}
