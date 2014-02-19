#include "iio-private.h"

#include <errno.h>
#include <stdio.h>

const char * iio_device_get_id(const struct iio_device *dev)
{
	return dev->id;
}

const char * iio_device_get_name(const struct iio_device *dev)
{
	return dev->name;
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

unsigned int iio_device_get_attrs_count(const struct iio_device *dev)
{
	return dev->nb_attrs;
}

const char * iio_device_get_attr(const struct iio_device *dev,
		unsigned int index)
{
	if (index >= dev->nb_attrs)
		return NULL;
	else
		return dev->attrs[index];
}

ssize_t iio_device_attr_read(const struct iio_device *dev,
		const char *attr, char *dst, size_t len)
{
	if (dev->ctx->ops->read_device_attr)
		return dev->ctx->ops->read_device_attr(dev, attr, dst, len);
	else
		return -ENOSYS;
}

ssize_t iio_device_attr_write(const struct iio_device *dev,
		const char *attr, const char *src)
{
	if (dev->ctx->ops->write_device_attr)
		return dev->ctx->ops->write_device_attr(dev, attr, src);
	else
		return -ENOSYS;
}

void free_device(struct iio_device *dev)
{
	unsigned int i;
	for (i = 0; i < dev->nb_attrs; i++)
		free((char *) dev->attrs[i]);
	if (dev->nb_attrs)
		free(dev->attrs);
	for (i = 0; i < dev->nb_channels; i++)
		free_channel(dev->channels[i]);
	if (dev->nb_channels)
		free(dev->channels);
	if (dev->name)
		free((char *) dev->name);
	if (dev->id)
		free((char *) dev->id);
	free(dev);
}
