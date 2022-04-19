// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "iio-private.h"

#include <iio/iio-debug.h>
#include <inttypes.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static ssize_t iio_snprintf_xml_attr(char *buf, ssize_t len, const char *attr,
				     enum iio_attr_type type)
{
	switch (type) {
		case IIO_ATTR_TYPE_DEVICE:
			return iio_snprintf(buf, len, "<attribute name=\"%s\" />", attr);
		case IIO_ATTR_TYPE_DEBUG:
			return iio_snprintf(buf, len, "<debug-attribute name=\"%s\" />", attr);
		case IIO_ATTR_TYPE_BUFFER:
			return iio_snprintf(buf, len, "<buffer-attribute name=\"%s\" />", attr);
		default:
			return -EINVAL;
	}
}

ssize_t iio_snprintf_device_xml(char *ptr, ssize_t len,
				const struct iio_device *dev)
{
	ssize_t ret, alen = 0;
	unsigned int i;

	ret = iio_snprintf(ptr, len, "<device id=\"%s\"", dev->id);
	if (ret < 0)
		return ret;

	iio_update_xml_indexes(ret, &ptr, &len, &alen);
	if (dev->name) {
		ret = iio_snprintf(ptr, len, " name=\"%s\"", dev->name);
		if (ret < 0)
			return ret;
	
		iio_update_xml_indexes(ret, &ptr, &len, &alen);
	}

	if (dev->label) {
		ret = iio_snprintf(ptr, len, " label=\"%s\"", dev->label);
		if (ret < 0)
			return ret;

		iio_update_xml_indexes(ret, &ptr, &len, &alen);
	}

	ret = iio_snprintf(ptr, len, " >");
	if (ret < 0)
		return ret;
	
	iio_update_xml_indexes(ret, &ptr, &len, &alen);

	for (i = 0; i < dev->nb_channels; i++) {
		ret = iio_snprintf_channel_xml(ptr, len, dev->channels[i]);
		if (ret < 0)
			return ret;

		iio_update_xml_indexes(ret, &ptr, &len, &alen);
	}

	for (i = 0; i < dev->attrs.num; i++) {
		ret = iio_snprintf_xml_attr(ptr, len, dev->attrs.names[i],
					    IIO_ATTR_TYPE_DEVICE);
		if (ret < 0)
			return ret;
		
		iio_update_xml_indexes(ret, &ptr, &len, &alen);
	}

	for (i = 0; i < dev->buffer_attrs.num; i++) {
		ret = iio_snprintf_xml_attr(ptr, len, dev->buffer_attrs.names[i],
					    IIO_ATTR_TYPE_BUFFER);
		if (ret < 0)
			return ret;

		iio_update_xml_indexes(ret, &ptr, &len, &alen);
	}

	for (i = 0; i < dev->debug_attrs.num; i++) {
		ret = iio_snprintf_xml_attr(ptr, len, dev->debug_attrs.names[i],
					    IIO_ATTR_TYPE_DEBUG);
		if (ret < 0)
			return ret;

		iio_update_xml_indexes(ret, &ptr, &len, &alen);
	}

	ret = iio_snprintf(ptr, len, "</device>");
	if (ret < 0)
		return ret;

	return alen + ret;
}

int add_iio_dev_attr(struct iio_device *dev, struct iio_dev_attrs *attrs,
		     const char *attr, const char *type)
{
	char **names, *name;

	name = iio_strdup(attr);
	if (!name)
		return -ENOMEM;

	names = realloc(attrs->names, (1 + attrs->num) * sizeof(char *));
	if (!names) {
		free(name);
		return -ENOMEM;
	}

	names[attrs->num++] = name;
	attrs->names = names;

	dev_dbg(dev, "Added%s attr \'%s\' to device \'%s\'\n", type, attr, dev->id);
	return 0;
}

const char * iio_device_get_id(const struct iio_device *dev)
{
	return dev->id;
}

const char * iio_device_get_name(const struct iio_device *dev)
{
	return dev->name;
}

const char * iio_device_get_label(const struct iio_device *dev)
{
	return dev->label;
}

unsigned int iio_device_get_channels_count(const struct iio_device *dev)
{
	return dev->nb_channels;
}

struct iio_channel * iio_device_get_channel(const struct iio_device *dev,
		unsigned int index)
{
	if (index >= dev->nb_channels)
		return NULL;
	else
		return dev->channels[index];
}

struct iio_channel * iio_device_find_channel(const struct iio_device *dev,
		const char *name, bool output)
{
	unsigned int i;
	for (i = 0; i < dev->nb_channels; i++) {
		struct iio_channel *chn = dev->channels[i];
		if (iio_channel_is_output(chn) != output)
			continue;

		if (!strcmp(chn->id, name) ||
				(chn->name && !strcmp(chn->name, name)))
			return chn;
	}
	return NULL;
}

static const char * iio_device_get_dev_attr(const struct iio_dev_attrs *attrs,
		unsigned int index)
{
	if (index >= attrs->num)
		return NULL;
	else
		return attrs->names[index];
}

const char * iio_device_find_dev_attr(const struct iio_dev_attrs *attrs,
		const char *name)
{
	unsigned int i;
	for (i = 0; i < attrs->num; i++) {
		const char *attr = attrs->names[i];
		if (!strcmp(attr, name))
			return attr;
	}
	return NULL;
}

unsigned int iio_device_get_attrs_count(const struct iio_device *dev)
{
	return dev->attrs.num;
}

const char * iio_device_get_attr(const struct iio_device *dev,
		unsigned int index)
{
	return iio_device_get_dev_attr(&dev->attrs, index);
}

const char * iio_device_find_attr(const struct iio_device *dev,
		const char *name)
{
	return iio_device_find_dev_attr(&dev->attrs, name);
}

unsigned int iio_device_get_buffer_attrs_count(const struct iio_device *dev)
{
	return dev->buffer_attrs.num;
}

const char * iio_device_get_buffer_attr(const struct iio_device *dev,
		unsigned int index)
{
	return iio_device_get_dev_attr(&dev->buffer_attrs, index);
}

const char * iio_device_find_buffer_attr(const struct iio_device *dev,
		const char *name)
{
	return iio_device_find_dev_attr(&dev->buffer_attrs, name);
}

const char * iio_device_find_debug_attr(const struct iio_device *dev,
		const char *name)
{
	return iio_device_find_dev_attr(&dev->debug_attrs, name);
}

bool iio_device_is_tx(const struct iio_device *dev)
{
	struct iio_channel *ch;
	unsigned int i;

	for (i = 0; i < dev->nb_channels; i++) {
		ch = dev->channels[i];
		if (iio_channel_is_output(ch) && iio_channel_is_scan_element(ch))
			return true;
	}

	return false;
}

ssize_t iio_device_attr_read_raw(const struct iio_device *dev,
		const char *attr, char *dst, size_t len)
{
	if (!attr)
		return -EINVAL;

	if (dev->ctx->ops->read_device_attr)
		return dev->ctx->ops->read_device_attr(dev, 0,
				attr, dst, len, IIO_ATTR_TYPE_DEVICE);
	else
		return -ENOSYS;
}

ssize_t iio_device_attr_write_raw(const struct iio_device *dev,
		const char *attr, const void *src, size_t len)
{
	if (!attr)
		return -EINVAL;

	if (dev->ctx->ops->write_device_attr)
		return dev->ctx->ops->write_device_attr(dev, 0,
				attr, src, len, IIO_ATTR_TYPE_DEVICE);
	else
		return -ENOSYS;
}

ssize_t iio_device_attr_write_string(const struct iio_device *dev,
				     const char *attr, const char *src)
{
	return iio_device_attr_write_raw(dev, attr, src, strlen(src) + 1);
}

ssize_t iio_device_buffer_attr_read_raw(const struct iio_device *dev,
					unsigned int buf_id, const char *attr,
					char *dst, size_t len)
{
	if (!attr)
		return -EINVAL;

	if (dev->ctx->ops->read_device_attr)
		return dev->ctx->ops->read_device_attr(dev, buf_id,
				attr, dst, len, IIO_ATTR_TYPE_BUFFER);
	else
		return -ENOSYS;
}

ssize_t iio_device_buffer_attr_write_raw(const struct iio_device *dev,
					 unsigned int buf_id, const char *attr,
					 const void *src, size_t len)
{
	if (!attr)
		return -EINVAL;

	if (dev->ctx->ops->write_device_attr)
		return dev->ctx->ops->write_device_attr(dev, buf_id,
				attr, src, len, IIO_ATTR_TYPE_BUFFER);
	else
		return -ENOSYS;
}

ssize_t iio_device_buffer_attr_write_string(const struct iio_device *dev,
					    unsigned int buf_id,
					    const char *attr, const char *src)
{
	return iio_device_buffer_attr_write_raw(dev, buf_id, attr, src,
						strlen(src) + 1);
}

void iio_device_set_data(struct iio_device *dev, void *data)
{
	dev->userdata = data;
}

void * iio_device_get_data(const struct iio_device *dev)
{
	return dev->userdata;
}

bool iio_device_is_trigger(const struct iio_device *dev)
{
	/* A trigger has a name, an id which starts by "trigger",
	 * and zero channels. */

	unsigned int nb = iio_device_get_channels_count(dev);
	const char *name = iio_device_get_name(dev),
	      *id = iio_device_get_id(dev);
	return ((nb == 0) && !!name &&
		!strncmp(id, "trigger", sizeof("trigger") - 1));
}

int iio_device_get_trigger(const struct iio_device *dev,
		const struct iio_device **trigger)
{
	if (!trigger)
		return -EINVAL;
	else if (dev->ctx->ops->get_trigger)
		return dev->ctx->ops->get_trigger(dev, trigger);
	else
		return -ENOSYS;
}

int iio_device_set_trigger(const struct iio_device *dev,
		const struct iio_device *trigger)
{
	if (trigger && !iio_device_is_trigger(trigger))
		return -EINVAL;
	else if (dev->ctx->ops->set_trigger)
		return dev->ctx->ops->set_trigger(dev, trigger);
	else
		return -ENOSYS;
}

static void free_iio_dev_attrs(struct iio_dev_attrs *attrs)
{
	unsigned int i;

	for (i = 0; i <  attrs->num; i++)
		free(attrs->names[i]);

	free(attrs->names);
}

void free_device(struct iio_device *dev)
{
	unsigned int i;

	free_iio_dev_attrs(&dev->attrs);
	free_iio_dev_attrs(&dev->buffer_attrs);
	free_iio_dev_attrs(&dev->debug_attrs);

	for (i = 0; i < dev->nb_channels; i++)
		free_channel(dev->channels[i]);
	free(dev->channels);
	free(dev->label);
	free(dev->name);
	free(dev->id);
	free(dev);
}

ssize_t iio_device_get_sample_size(const struct iio_device *dev,
				   const struct iio_channels_mask *mask)
{
	ssize_t size = 0;
	unsigned int i;
	const struct iio_channel *prev = NULL;

	if (mask->words != (dev->nb_channels + 31) / 32)
		return -EINVAL;

	for (i = 0; i < dev->nb_channels; i++) {
		const struct iio_channel *chn = dev->channels[i];
		unsigned int length = chn->format.length / 8 *
			chn->format.repeat;

		if (chn->index < 0)
			break;
		if (!iio_channels_mask_test_bit(mask, chn->number))
			continue;

		if (prev && chn->index == prev->index) {
			prev = chn;
			continue;
		}

		if (size % length)
			size += 2 * length - (size % length);
		else
			size += length;

		prev = chn;
	}
	return size;
}

int iio_device_attr_read_longlong(const struct iio_device *dev,
		const char *attr, long long *val)
{
	char *end, buf[1024];
	long long value;
	ssize_t ret;

	ret = iio_device_attr_read_raw(dev, attr, buf, sizeof(buf));
	if (ret < 0)
		return (int) ret;

	errno = 0;
	value = strtoll(buf, &end, 0);
	if (end == buf || errno == ERANGE)
		return -EINVAL;
	*val = value;
	return 0;
}

int iio_device_attr_read_bool(const struct iio_device *dev,
		const char *attr, bool *val)
{
	long long value;
	int ret;

	ret = iio_device_attr_read_longlong(dev, attr, &value);
	if (ret < 0)
		return ret;

	*val = !!value;
	return 0;
}

int iio_device_attr_read_double(const struct iio_device *dev,
		const char *attr, double *val)
{
	char buf[1024];
	ssize_t ret;

	ret = iio_device_attr_read_raw(dev, attr, buf, sizeof(buf));
	if (ret < 0)
		return (int) ret;
	else
		return read_double(buf, val);
}

int iio_device_attr_write_longlong(const struct iio_device *dev,
		const char *attr, long long val)
{
	ssize_t ret;
	char buf[1024];

	iio_snprintf(buf, sizeof(buf), "%lld", val);
	ret = iio_device_attr_write_string(dev, attr, buf);

	return (int) (ret < 0 ? ret : 0);
}

int iio_device_attr_write_double(const struct iio_device *dev,
		const char *attr, double val)
{
	ssize_t ret;
	char buf[1024];

	ret = (ssize_t) write_double(buf, sizeof(buf), val);
	if (!ret)
		ret = iio_device_attr_write_string(dev, attr, buf);
	return (int) (ret < 0 ? ret : 0);
}

int iio_device_attr_write_bool(const struct iio_device *dev,
		const char *attr, bool val)
{
	ssize_t ret;

	if (val)
		ret = iio_device_attr_write_string(dev, attr, "1");
	else
		ret = iio_device_attr_write_string(dev, attr, "0");

	return (int) (ret < 0 ? ret : 0);
}

int iio_device_buffer_attr_read_longlong(const struct iio_device *dev,
					 unsigned int buf_id,
					 const char *attr, long long *val)
{
	char *end, buf[1024];
	long long value;
	ssize_t ret;

	ret = iio_device_buffer_attr_read_raw(dev, buf_id, attr,
					      buf, sizeof(buf));
	if (ret < 0)
		return (int) ret;

	errno = 0;
	value = strtoll(buf, &end, 0);
	if (end == buf || errno == ERANGE)
		return -EINVAL;
	*val = value;
	return 0;
}

int iio_device_buffer_attr_read_bool(const struct iio_device *dev,
				     unsigned int buf_id,
				     const char *attr, bool *val)
{
	long long value;
	int ret;

	ret = iio_device_buffer_attr_read_longlong(dev, buf_id, attr, &value);
	if (ret < 0)
		return ret;

	*val = !!value;
	return 0;
}

int iio_device_buffer_attr_read_double(const struct iio_device *dev,
				       unsigned int buf_id,
				       const char *attr, double *val)
{
	char buf[1024];
	ssize_t ret;

	ret = iio_device_buffer_attr_read_raw(dev, buf_id, attr,
					      buf, sizeof(buf));
	if (ret < 0)
		return (int) ret;
	else
		return read_double(buf, val);
}

int iio_device_buffer_attr_write_longlong(const struct iio_device *dev,
					  unsigned int buf_id,
					  const char *attr, long long val)
{
	ssize_t ret;
	char buf[1024];

	iio_snprintf(buf, sizeof(buf), "%lld", val);
	ret = iio_device_buffer_attr_write_string(dev, buf_id, attr, buf);

	return (int) (ret < 0 ? ret : 0);
}

int iio_device_buffer_attr_write_double(const struct iio_device *dev,
					unsigned int buf_id,
					const char *attr, double val)
{
	ssize_t ret;
	char buf[1024];

	ret = (ssize_t) write_double(buf, sizeof(buf), val);
	if (!ret)
		ret = iio_device_buffer_attr_write_string(dev, buf_id,
							  attr, buf);
	return (int) (ret < 0 ? ret : 0);
}

int iio_device_buffer_attr_write_bool(const struct iio_device *dev,
				      unsigned int buf_id,
				      const char *attr, bool val)
{
	ssize_t ret;
	const char *value = val ? "1" : "0";

	ret = iio_device_buffer_attr_write_string(dev, buf_id, attr, value);

	return (int) (ret < 0 ? ret : 0);
}

ssize_t iio_device_debug_attr_read_raw(const struct iio_device *dev,
		const char *attr, char *dst, size_t len)
{
	if (!attr)
		return -EINVAL;

	if (dev->ctx->ops->read_device_attr)
		return dev->ctx->ops->read_device_attr(dev, 0,
				attr, dst, len, IIO_ATTR_TYPE_DEBUG);
	else
		return -ENOSYS;
}

ssize_t iio_device_debug_attr_write_raw(const struct iio_device *dev,
		const char *attr, const void *src, size_t len)
{
	if (!attr)
		return -EINVAL;

	if (dev->ctx->ops->write_device_attr)
		return dev->ctx->ops->write_device_attr(dev, 0,
				attr, src, len, IIO_ATTR_TYPE_DEBUG);
	else
		return -ENOSYS;
}

ssize_t iio_device_debug_attr_write_string(const struct iio_device *dev,
		const char *attr, const char *src)
{
	return iio_device_debug_attr_write_raw(dev, attr, src, strlen(src) + 1);
}

unsigned int iio_device_get_debug_attrs_count(const struct iio_device *dev)
{
	return dev->debug_attrs.num;
}

const char * iio_device_get_debug_attr(const struct iio_device *dev,
		unsigned int index)
{
	return iio_device_get_dev_attr(&dev->debug_attrs, index);
}

int iio_device_debug_attr_read_longlong(const struct iio_device *dev,
		const char *attr, long long *val)
{
	char *end, buf[1024];
	long long value;
	ssize_t ret = iio_device_debug_attr_read_raw(dev, attr, buf, sizeof(buf));
	if (ret < 0)
		return (int) ret;

	errno = 0;
	value = strtoll(buf, &end, 0);
	if (end == buf || errno == ERANGE)
		return -EINVAL;
	*val = value;
	return 0;
}

int iio_device_debug_attr_read_bool(const struct iio_device *dev,
		const char *attr, bool *val)
{
	long long value;
	int ret = iio_device_debug_attr_read_longlong(dev, attr, &value);
	if (ret < 0)
		return ret;

	*val = !!value;
	return 0;
}

int iio_device_debug_attr_read_double(const struct iio_device *dev,
		const char *attr, double *val)
{
	char buf[1024];
	ssize_t ret = iio_device_debug_attr_read_raw(dev, attr, buf, sizeof(buf));
	if (ret < 0)
		return (int) ret;
	else
		return read_double(buf, val);
}

int iio_device_debug_attr_write_longlong(const struct iio_device *dev,
		const char *attr, long long val)
{
	ssize_t ret;
	char buf[1024];

	iio_snprintf(buf, sizeof(buf), "%lld", val);
	ret = iio_device_debug_attr_write_string(dev, attr, buf);

	return (int) (ret < 0 ? ret : 0);
}

int iio_device_debug_attr_write_double(const struct iio_device *dev,
		const char *attr, double val)
{
	ssize_t ret;
	char buf[1024];

	ret = (ssize_t) write_double(buf, sizeof(buf), val);
	if (!ret)
		ret = iio_device_debug_attr_write_string(dev, attr, buf);
	return (int) (ret < 0 ? ret : 0);
}

int iio_device_debug_attr_write_bool(const struct iio_device *dev,
		const char *attr, bool val)
{
	ssize_t ret;

	if (val)
		ret = iio_device_debug_attr_write_raw(dev, attr, "1", 2);
	else
		ret = iio_device_debug_attr_write_raw(dev, attr, "0", 2);

	return (int) (ret < 0 ? ret : 0);
}

int iio_device_identify_filename(const struct iio_device *dev,
		const char *filename, struct iio_channel **chn,
		const char **attr)
{
	unsigned int i;

	for (i = 0; i < dev->nb_channels; i++) {
		struct iio_channel *ch = dev->channels[i];
		unsigned int j;

		for (j = 0; j < ch->nb_attrs; j++) {
			if (!strcmp(ch->attrs[j].filename, filename)) {
				*attr = ch->attrs[j].name;
				*chn = ch;
				return 0;
			}
		}
	}

	for (i = 0; i < dev->attrs.num; i++) {
		/* Devices attributes are named after their filename */
		if (!strcmp(dev->attrs.names[i], filename)) {
			*attr = dev->attrs.names[i];
			*chn = NULL;
			return 0;
		}
	}

	for (i = 0; i < dev->debug_attrs.num; i++) {
		if (!strcmp(dev->debug_attrs.names[i], filename)) {
			*attr = dev->debug_attrs.names[i];
			*chn = NULL;
			return 0;
		}
	}

	return -EINVAL;
}

int iio_device_reg_write(struct iio_device *dev,
		uint32_t address, uint32_t value)
{
	ssize_t ret;
	char buf[1024];

	iio_snprintf(buf, sizeof(buf), "0x%" PRIx32 " 0x%" PRIx32,
			address, value);
	ret = iio_device_debug_attr_write_string(dev, "direct_reg_access", buf);

	return (int) (ret < 0 ? ret : 0);
}

int iio_device_reg_read(struct iio_device *dev,
		uint32_t address, uint32_t *value)
{
	/* NOTE: There is a race condition here. But it is extremely unlikely to
	 * happen, and as this is a debug function, it shouldn't be used for
	 * something else than debug. */

	long long val;
	int ret = iio_device_debug_attr_write_longlong(dev,
			"direct_reg_access", (long long) address);
	if (ret < 0)
		return ret;

	ret = iio_device_debug_attr_read_longlong(dev,
			"direct_reg_access", &val);
	if (!ret)
		*value = (uint32_t) val;
	return ret;
}

const struct iio_context * iio_device_get_context(const struct iio_device *dev)
{
	return dev->ctx;
}

struct iio_device_pdata * iio_device_get_pdata(const struct iio_device *dev)
{
	return dev->pdata;
}

void iio_device_set_pdata(struct iio_device *dev, struct iio_device_pdata *d)
{
	dev->pdata = d;
}
