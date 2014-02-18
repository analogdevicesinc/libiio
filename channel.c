#include "debug.h"
#include "iio-private.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

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
	return chn->dev->ctx->ops->read_channel_attr(chn, attr, dst, len);
}

ssize_t iio_channel_attr_write(const struct iio_channel *chn,
		const char *attr, const char *src)
{
	return chn->dev->ctx->ops->write_channel_attr(chn, attr, src);
}

void free_channel(struct iio_channel *chn)
{
	unsigned int i;
	for (i = 0; i < chn->nb_attrs; i++)
		free((char *) chn->attrs[i]);
	if (chn->nb_attrs)
		free(chn->attrs);
	if (chn->name)
		free((char *) chn->name);
	if (chn->id)
		free((char *) chn->id);
	free(chn);
}
