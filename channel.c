#include "iio-private.h"

#include <stdio.h>

static const char * get_channel_type(const struct iio_channel *chn)
{
	switch (iio_channel_get_type(chn)) {
	case IIO_CHANNEL_INPUT:
		return "in";
	case IIO_CHANNEL_OUTPUT:
		return "out";
	default:
		return "unknown";
	};
}

const char * iio_channel_get_id(const struct iio_channel *chn)
{
	return chn->id;
}

const char * iio_channel_get_name(const struct iio_channel *chn)
{
	return chn->name;
}

enum iio_channel_type iio_channel_get_type(const struct iio_channel *chn)
{
	return chn->type;
}

unsigned int iio_channel_get_attrs_count(const struct iio_channel *chn)
{
	return chn->nb_attrs;
}

const char * iio_channel_get_attr(const struct iio_channel *chn,
		unsigned int index)
{
	if (index >= chn->nb_attrs)
		return NULL;
	else
		return chn->attrs[index];
}

ssize_t iio_channel_attr_read(const struct iio_channel *chn,
		const char *attr, char *dst, size_t len)
{
	char buf[1024];
	const char *type = get_channel_type(chn);
	struct iio_device *dev = chn->dev;

	/* TODO(pcercuei): This does not work with shared attributes */
	if (chn->name)
		sprintf(buf, "%s_%s_%s_%s", type, chn->id, chn->name, attr);
	else
		sprintf(buf, "%s_%s_%s", type, chn->id, attr);
	return dev->ctx->ops->read_attr(dev, buf, dst, len);
}

ssize_t iio_channel_attr_write(const struct iio_channel *chn,
		const char *attr, const char *src)
{
	char buf[1024];
	const char *type = get_channel_type(chn);
	struct iio_device *dev = chn->dev;

	/* TODO(pcercuei): This does not work with shared attributes */
	if (chn->name)
		sprintf(buf, "%s_%s_%s_%s", type, chn->id, chn->name, attr);
	else
		sprintf(buf, "%s_%s_%s", type, chn->id, attr);
	return dev->ctx->ops->write_attr(dev, buf, src);
}
