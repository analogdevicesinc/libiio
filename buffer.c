// SPDX-License-Identifier: MIT
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014-2015 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "attr.h"
#include "iio-config.h"
#include "iio-private.h"

#include <errno.h>
#include <iio/iio-debug.h>
#include <iio/iio-lock.h>
#include <string.h>

void iio_buffer_set_data(struct iio_buffer *buf, void *data)
{
	buf->userdata = data;
}

void * iio_buffer_get_data(const struct iio_buffer *buf)
{
	return buf->userdata;
}

const struct iio_device * iio_buffer_get_device(const struct iio_buffer *buf)
{
	return buf->dev;
}

void iio_buffer_stream_cancel(struct iio_buffer_stream *buf_stream)
{
	const struct iio_backend_ops *ops = buf_stream->buf->dev->ctx->ops;

	iio_task_stop(buf_stream->worker);

	if (ops->cancel_buffer)
		ops->cancel_buffer(buf_stream->pdata);

	iio_task_flush(buf_stream->worker);
}

static int iio_buffer_stream_set_enabled(const struct iio_buffer_stream *buf_stream, bool enabled)
{
	struct iio_buffer *buf = buf_stream->buf;
	const struct iio_backend_ops *ops = buf->dev->ctx->ops;
	size_t sample_size, nb_samples = 0;
	bool cyclic = false;

	if (buf_stream->block_size) {
		sample_size = iio_device_get_sample_size(buf->dev, buf_stream->mask);
		nb_samples = buf_stream->block_size / sample_size;
		cyclic = buf_stream->cyclic;
	}

	if (ops->enable_buffer)
		return ops->enable_buffer(buf_stream->pdata, nb_samples, enabled, cyclic);

	return -ENOSYS;
}

int iio_buffer_stream_start(struct iio_buffer_stream *buf_stream)
{
	int err;

	if (buf_stream->nb_blocks == 0) {
		dev_err(buf_stream->buf->dev,
			"Cannot start buffer before creating blocks.\n");
		return -EINVAL;
	}

	err = iio_buffer_stream_set_enabled(buf_stream, true);
	if (err < 0 && err != -ENOSYS)
		return err;

	iio_task_start(buf_stream->worker);

	return 0;
}

int iio_buffer_stream_stop(struct iio_buffer_stream *buf_stream)
{
	int err;

	err = iio_buffer_stream_set_enabled(buf_stream, false);
	if (err < 0 && err != -ENOSYS)
		return err;

	iio_task_stop(buf_stream->worker);

	return 0;
}

static int iio_buffer_stream_enqueue_worker(void *_, void *d)
{
	return iio_block_io(d);
}

struct iio_buffer_stream *
iio_buffer_open(struct iio_buffer *buf, const struct iio_channels_mask *mask)
{
	const struct iio_device *dev = buf->dev;
	const struct iio_backend_ops *ops = dev->ctx->ops;
	struct iio_buffer_stream *buf_stream;
	ssize_t sample_size;
	int err;

	if (!ops->open_buffer)
		return iio_ptr(-ENOSYS);

	sample_size = iio_device_get_sample_size(dev, mask);
	if (sample_size < 0)
		return iio_ptr((int) sample_size);
	if (!sample_size)
               return iio_ptr(-EINVAL);

	buf_stream = zalloc(sizeof(*buf_stream));
	if (!buf_stream)
		return iio_ptr(-ENOMEM);

	buf_stream->buf = buf;

	buf_stream->mask = iio_create_channels_mask(dev->nb_channels);
	if (!buf_stream->mask) {
		err = -ENOMEM;
		goto err_free_bs;
	}

	err = iio_channels_mask_copy(buf_stream->mask, mask);
	if (err)
		goto err_free_mask;

	buf_stream->lock = iio_mutex_create();
	err = iio_err(buf_stream->lock);
	if (err)
		goto err_free_mask;

	buf_stream->worker = iio_task_create(iio_buffer_stream_enqueue_worker, NULL,
                                    "enqueue-worker");
	err = iio_err(buf_stream->worker);
	if (err < 0)
		goto err_free_mutex;

	buf_stream->pdata = ops->open_buffer(dev, buf->idx, buf_stream->mask);
	err = iio_err(buf_stream->pdata);
	if (err < 0)
		goto err_destroy_worker;

	return buf_stream;

err_destroy_worker:
	iio_task_destroy(buf_stream->worker);
err_free_mutex:
	iio_mutex_destroy(buf_stream->lock);
err_free_mask:
	iio_channels_mask_destroy(buf_stream->mask);
err_free_bs:
	free(buf_stream);
	return iio_ptr(err);
}

void iio_buffer_close(struct iio_buffer_stream *buf_stream)
{
       const struct iio_backend_ops *ops = buf_stream->buf->dev->ctx->ops;

       iio_buffer_stream_cancel(buf_stream);

       if (ops->close_buffer)
               ops->close_buffer(buf_stream->pdata);

       iio_task_destroy(buf_stream->worker);
       iio_mutex_destroy(buf_stream->lock);
       iio_channels_mask_destroy(buf_stream->mask);
       free(buf_stream);
}

const struct iio_channels_mask *
iio_buffer_stream_get_channels_mask(const struct iio_buffer_stream *buf_stream)
{
	return buf_stream->mask;
}

unsigned int iio_buffer_get_attrs_count(const struct iio_buffer *buf)
{
	return buf->attrlist.num;
}

const struct iio_attr *
iio_buffer_get_attr(const struct iio_buffer *buf, unsigned int index)
{
	return iio_attr_get(&buf->attrlist, index);
}

const struct iio_attr *
iio_buffer_find_attr(const struct iio_buffer *buf, const char *name)
{
	return iio_attr_find(&buf->attrlist, name);
}

void free_buffer(struct iio_buffer *buf)
{
	iio_free_attrs(&buf->attrlist);
	free(buf);
}
