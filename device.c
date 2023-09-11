// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "attr.h"
#include "iio-private.h"

#include <iio/iio-debug.h>
#include <inttypes.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static const char * const xml_attr_prefix[] = {
	"",
	"debug-",
	"buffer-",
};

static ssize_t iio_snprintf_xml_attr(const struct iio_attr *attr,
				     char *buf, ssize_t len)
{
	switch (attr->type) {
		case IIO_ATTR_TYPE_DEVICE:
		case IIO_ATTR_TYPE_DEBUG:
		case IIO_ATTR_TYPE_BUFFER:
			return iio_snprintf(buf, len,
					    "<%sattribute name=\"%s\" />",
					    xml_attr_prefix[attr->type],
					    attr->name);
		default:
			return -EINVAL;
	}
}

ssize_t iio_snprintf_device_xml(char *ptr, ssize_t len,
				const struct iio_device *dev)
{
	const struct iio_attr *attrs;
	ssize_t ret, alen = 0;
	unsigned int i;
	enum iio_attr_type type;

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

	for (type = IIO_ATTR_TYPE_DEVICE; type <= IIO_ATTR_TYPE_BUFFER; type++) {
		for (i = 0; i < dev->attrlist[type].num; i++) {
			attrs = dev->attrlist[type].attrs;

			ret = iio_snprintf_xml_attr(&attrs[i], ptr, len);
			if (ret < 0)
				return ret;

			iio_update_xml_indexes(ret, &ptr, &len, &alen);
		}
	}

	ret = iio_snprintf(ptr, len, "</device>");
	if (ret < 0)
		return ret;

	return alen + ret;
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

unsigned int iio_device_get_attrs_count(const struct iio_device *dev)
{
	return dev->attrlist[IIO_ATTR_TYPE_DEVICE].num;
}

const struct iio_attr *
iio_device_get_attr(const struct iio_device *dev, unsigned int index)
{
	return iio_attr_get(&dev->attrlist[IIO_ATTR_TYPE_DEVICE], index);
}

const struct iio_attr *
iio_device_find_attr(const struct iio_device *dev, const char *name)
{
	return iio_attr_find(&dev->attrlist[IIO_ATTR_TYPE_DEVICE], name);
}

unsigned int iio_device_get_debug_attrs_count(const struct iio_device *dev)
{
	return dev->attrlist[IIO_ATTR_TYPE_DEBUG].num;
}

const struct iio_attr *
iio_device_get_debug_attr(const struct iio_device *dev, unsigned int index)
{
	return iio_attr_get(&dev->attrlist[IIO_ATTR_TYPE_DEBUG], index);
}

const struct iio_attr *
iio_device_find_debug_attr(const struct iio_device *dev, const char *name)
{
	return iio_attr_find(&dev->attrlist[IIO_ATTR_TYPE_DEBUG], name);
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

const struct iio_device *
iio_device_get_trigger(const struct iio_device *dev)
{
	if (dev->ctx->ops->get_trigger)
		return dev->ctx->ops->get_trigger(dev);
	else
		return iio_ptr(-ENOSYS);
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

void free_device(struct iio_device *dev)
{
	enum iio_attr_type type;
	unsigned int i;

	for (type = IIO_ATTR_TYPE_DEVICE; type <= IIO_ATTR_TYPE_BUFFER; type++)
		iio_free_attrs(&dev->attrlist[type]);

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
	unsigned int i, largest = 1;
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

		if (length > largest)
			largest = length;

		if (size % length)
			size += 2 * length - (size % length);
		else
			size += length;

		prev = chn;
	}

	if (size % largest)
		size += largest - (size % largest);

	return size;
}

int iio_device_reg_write(struct iio_device *dev,
		uint32_t address, uint32_t value)
{
	const struct iio_attr *attr;
	ssize_t ret;
	char buf[1024];

	attr = iio_device_find_debug_attr(dev, "direct_reg_access");
	if (!attr)
		return -ENOENT;

	iio_snprintf(buf, sizeof(buf), "0x%" PRIx32 " 0x%" PRIx32,
			address, value);

	ret = iio_attr_write_string(attr, buf);

	return (int) (ret < 0 ? ret : 0);
}

int iio_device_reg_read(struct iio_device *dev,
		uint32_t address, uint32_t *value)
{
	/* NOTE: There is a race condition here. But it is extremely unlikely to
	 * happen, and as this is a debug function, it shouldn't be used for
	 * something else than debug. */

	const struct iio_attr *attr;
	long long val;
	int ret;

	attr = iio_device_find_debug_attr(dev, "direct_reg_access");
	if (!attr)
		return -ENOENT;

	ret = iio_attr_write_longlong(attr, (long long) address);
	if (ret < 0)
		return ret;

	ret = iio_attr_read_longlong(attr, &val);
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
