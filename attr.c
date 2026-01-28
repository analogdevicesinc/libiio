// SPDX-License-Identifier: MIT
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2023 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "iio-private.h"
#include "sort.h"

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

	iio_sort_attrs(attrs);

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

	dev_dbg(dev, "Added%s attr \'%s\'\n", attr_type_string[type], name);
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

	chn_dbg(chn, "Added attr \'%s\' (\'%s\')\n", name, filename);
	return 0;
}

int iio_buffer_add_attr(struct iio_buffer *buf, const char *name)
{
	union iio_pointer p = { .buf = buf, };
	int ret;

	ret = iio_add_attr(p, &buf->attrlist, name, NULL, IIO_ATTR_TYPE_BUFFER);
	if (ret < 0)
		return ret;

	dev_dbg(buf->dev, "Added buffer attr \'%s\'\n", name);
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

	/* Keep attr values in sync with the names which just got sorted in iio_add_attr() */
	unsigned int j, new_idx = ctx->attrlist.num - 1;

	/* Find the new index of the added attr */
	for (j = 0; j < ctx->attrlist.num - 1; j++) {
		if (!strcmp(ctx->attrlist.attrs[j].name, key)) {
			new_idx = j;
			break;
		}
	}

	/* If the new position is not at the end, it was inserted at a position
	causing subsequent attributes to shift forward as they were already sorted */
	if (new_idx != ctx->attrlist.num - 1) {
		memmove(&ctx->values[new_idx + 1], &ctx->values[new_idx],
			(ctx->attrlist.num - new_idx - 1) * sizeof(*ctx->values));
		ctx->values[new_idx] = new_val;
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

int iio_attr_get_range(const struct iio_attr *attr, double *min, double *step, double *max)
{
	double lmin, lstep, lmax;
	char extra;
	ssize_t ret;
	int n;

	if (!attr)
		return -EINVAL;

	/* As of now there are no available attributes for buffers and the
	 * below wrongly detects data_available as an available kind of
	 * attribute. If we start to see more exceptions or buffers with
	 * valid available attributes, we can think about something like
	 * a blocklist array.
	 */
	if (attr->type == IIO_ATTR_TYPE_BUFFER)
		return -ENXIO;

	if (!string_ends_with(iio_attr_get_name(attr), "available"))
		return -ENXIO;

	char *buf = malloc(MAX_ATTR_VALUE);
	if (!buf)
		return -ENOMEM;

	ret = iio_attr_read_raw(attr, buf, MAX_ATTR_VALUE);
	if (ret < 0) {
		free(buf);
		return (int) ret;
	}

	// Expect format: [min step max]
#if defined(_MSC_VER)
	n = iio_sscanf(buf, " [ %lf %lf %lf %c", &lmin, &lstep, &lmax, &extra, (unsigned int)sizeof(extra));
#else
	n = iio_sscanf(buf, " [ %lf %lf %lf %c", &lmin, &lstep, &lmax, &extra);
#endif

	if (n == 4 && extra == ']') {
		*min = lmin;
		*step = lstep;
		*max = lmax;
	} else {
		free(buf);
		return -EOPNOTSUPP;
	}

	free(buf);
	return 0;
}

int iio_attr_get_available(const struct iio_attr *attr, char ***list, size_t *count)
{
	size_t n = 0;
	size_t capacity = 4;
	char *saveptr;
	char **local_list = NULL;
	int ret;

	if (!attr)
		return -EINVAL;

	/* As of now there are no available attributes for buffers and the
	 * below wrongly detects data_available as an available kind of
	 * attribute. If we start to see more exceptions or buffers with
	 * valid available attributes, we can think about something like
	 * a blocklist array.
	 */
	if (attr->type == IIO_ATTR_TYPE_BUFFER)
		return -ENXIO;

	if (!string_ends_with(iio_attr_get_name(attr), "available"))
		return -ENXIO;

	char *buf = malloc(MAX_ATTR_VALUE);
	if (!buf)
		return -ENOMEM;

	ret = (int)iio_attr_read_raw(attr, buf, MAX_ATTR_VALUE);
	if (ret < 0) {
		free(buf);
		return ret;
	}

	if (buf[0] == '[') {
		free(buf);
		return -EOPNOTSUPP;
	}

	local_list = malloc(capacity * sizeof(char *));
	if (!local_list) {
		free(buf);
		return -ENOMEM;
	}

	char *token = iio_strtok_r(buf, " \n", &saveptr);
	while (token) {
		if (n >= capacity) {
			capacity *= 2;
			char **tmp = (char **)realloc(local_list, capacity * sizeof(char *));
			if (!tmp) {
				// Free previously allocated strings
				size_t i = 0;
				for (; i < n; ++i)
					free(local_list[i]);
				free(local_list);
				free(buf);
				return -ENOMEM;
			}
			local_list = tmp;
		}
		local_list[n++] = iio_strdup(token);
		token = iio_strtok_r(NULL, " \n", &saveptr);
	}

	free(buf);

	*list = local_list;
	*count = n;

    return 0;
}

void iio_available_list_free(char **list, size_t count)
{
	size_t i;

	if (list) {
		for (i = 0; i < count; i++)
			if (list[i])
				free(list[i]);
		free(list);
	}
}

int iio_attr_get_available_buf(const struct iio_attr *attr, char *buf,
	size_t buflen, char **list, size_t *count)
{
	ssize_t ret;
	size_t n = 0, max = (size_t)-1;
	char *p, *delim;
	size_t remaining;

	if (!buf || buflen == 0)
		return -EINVAL;

	/* As of now there are no available attributes for buffers and the
	 * below wrongly detects data_available as an available kind of
	 * attribute. If we start to see more exceptions or buffers with
	 * valid available attributes, we can think about something like
	 * a blocklist array.
	 */
	if (attr->type == IIO_ATTR_TYPE_BUFFER)
		return -ENXIO;

	if (!string_ends_with(iio_attr_get_name(attr), "available"))
		return -ENXIO;

	if (list && count)
		max = *count;
	else if (list && !count)
		max = buflen / 2 + 1; // heuristic: max tokens if all single chars and spaces

	ret = iio_attr_read_raw(attr, buf, buflen);
	if (ret < 0)
		return (int)ret;

	if (buf[0] == '[')
		return -EOPNOTSUPP;

	p = buf;
	remaining = buflen - (size_t)(p - buf);

	while (n < max && remaining && *p) {
		if (list)
			list[n] = p;
		n++;

		delim = (char *)memchr((void *)p, (int)' ', remaining);
		if (delim) {
			*delim = '\0';
			p = delim + 1;
			remaining = buflen - (size_t)(p - buf);
		} else {
			break;
		}
	}

	if (list && count && *p && n == max)
		return -ENOSPC;

	if (count)
		*count = n;

	return 0;
}
