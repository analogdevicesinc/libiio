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
#include <stdio.h>
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

void iio_buffer_cancel(struct iio_buffer *buf)
{
	const struct iio_backend_ops *ops = buf->dev->ctx->ops;

	iio_task_stop(buf->worker);

	if (ops->cancel_buffer)
		ops->cancel_buffer(buf->pdata);

	iio_task_flush(buf->worker);
}

static int iio_buffer_set_enabled(const struct iio_buffer *buf, bool enabled)
{
	const struct iio_backend_ops *ops = buf->dev->ctx->ops;
	size_t sample_size, nb_samples = 0;
	bool cyclic = false;

	printf("iio_buffer_set_enabled: block_size=%zu\n",
	       buf->block_size);

	if (buf->block_size) {
		sample_size = iio_device_get_sample_size(buf->dev, buf->mask);
		nb_samples = buf->block_size / sample_size;
		printf("iio_buffer_set_enabled: sample_size=%zu\n", sample_size);
		cyclic = buf->cyclic;
	}

	if (ops->enable_buffer)
		return ops->enable_buffer(buf->pdata, nb_samples, enabled, cyclic);

	return -ENOSYS;
}

int iio_buffer_enable(struct iio_buffer *buffer)
{
	int err;

	printf("iio_buffer_enable: nb_blocks=%u\n", buffer->nb_blocks);

	if (buffer->nb_blocks == 0) {
		dev_err(buffer->dev,
			"Cannot enable buffer before creating blocks.\n");
		return -EINVAL;
	}

	err = iio_buffer_set_enabled(buffer, true);
	if (err < 0 && err != -ENOSYS)
		return err;

	iio_task_start(buffer->worker);

	return 0;
}

int iio_buffer_disable(struct iio_buffer *buffer)
{
	int err;

	err = iio_buffer_set_enabled(buffer, false);
	if (err < 0 && err != -ENOSYS)
		return err;

	iio_task_stop(buffer->worker);

	return 0;
}

static int iio_buffer_enqueue_worker(void *_, void *d)
{
	return iio_block_io(d);
}

/*
 * \!TODO: Evaluate matching mask with a buffer mask (might be meaningfull for
 * multi-buffer devices). Things will error out anyways but the error won't be
 * very meaningful for users.
 */
struct iio_buffer *
iio_device_create_buffer(const struct iio_device *dev, unsigned int idx,
			 const struct iio_channels_mask *mask)
{
	const struct iio_backend_ops *ops = dev->ctx->ops;
	const struct iio_attr_list *src_attrlist;
	struct iio_buffer *buf;
	ssize_t sample_size;
	size_t attrlist_size;
	unsigned int i;
	int err;

	if (idx >= dev->nb_buffers) {
		printf("Requested buffer index %u but device has only %u buffers\n",
		       idx, dev->nb_buffers);
		return iio_ptr(-EINVAL);
	}
	if (!ops->create_buffer)
		return iio_ptr(-ENOSYS);

	sample_size = iio_device_get_sample_size(dev, mask);
	if (sample_size < 0) {
		printf("Failed to get sample size for buffer creation %d\n",
		       (int) sample_size);
		return iio_ptr((int) sample_size);
	}
	if (!sample_size)
		return iio_ptr(-EINVAL);

	buf = zalloc(sizeof(*buf));
	if (!buf)
		return iio_ptr(-ENOMEM);

	buf->dev = dev;
	buf->idx = idx;

	/* Duplicate buffer attributes from the iio_device.
	 * This ensures that those can contain a pointer to our iio_buffer.
	 * For multi-buffer devices, use per-buffer attrlist; otherwise use legacy. */
	if (dev->nb_buffers > 1)
		src_attrlist = &dev->buffers[idx].attrlist;
	else
		src_attrlist = &dev->attrlist[IIO_ATTR_TYPE_BUFFER];

	buf->attrlist.num = src_attrlist->num;
	attrlist_size = buf->attrlist.num * sizeof(*buf->attrlist.attrs);
	buf->attrlist.attrs = malloc(attrlist_size);
	if (!buf->attrlist.attrs) {
		err = -ENOMEM;
		goto err_free_buf;
	}

	memcpy(buf->attrlist.attrs, /* Flawfinder: ignore */
	       src_attrlist->attrs, attrlist_size);

	for (i = 0; i < buf->attrlist.num; i++)
		buf->attrlist.attrs[i].iio.buf = buf;

	buf->mask = iio_create_channels_mask(dev->nb_channels);
	if (!buf->mask) {
		err = -ENOMEM;
		goto err_free_attrs;
	}

	err = iio_channels_mask_copy(buf->mask, mask);
	if (err)
		goto err_free_mask;

	buf->lock = iio_mutex_create();
	err = iio_err(buf->lock);
	if (err)
		goto err_free_mask;

	buf->worker = iio_task_create(iio_buffer_enqueue_worker, NULL,
				      "enqueue-worker");
	err = iio_err(buf->worker);
	if (err < 0)
		goto err_free_mutex;

	buf->pdata = ops->create_buffer(dev, idx, buf->mask);
	err = iio_err(buf->pdata);
	if (err < 0)
		goto err_destroy_worker;

	return buf;

err_destroy_worker:
	iio_task_destroy(buf->worker);
err_free_mutex:
	iio_mutex_destroy(buf->lock);
err_free_mask:
	iio_channels_mask_destroy(buf->mask);
err_free_attrs:
	/* No need to call iio_free_attrs() since the names / filenames
	 * are allocated by the device */
	free(buf->attrlist.attrs);
err_free_buf:
	free(buf);
	return iio_ptr(err);
}

void iio_buffer_destroy(struct iio_buffer *buf)
{
	const struct iio_backend_ops *ops = buf->dev->ctx->ops;

	iio_buffer_cancel(buf);

	if (ops->free_buffer)
		ops->free_buffer(buf->pdata);

	iio_task_destroy(buf->worker);
	iio_mutex_destroy(buf->lock);
	iio_channels_mask_destroy(buf->mask);
	free(buf->attrlist.attrs);
	free(buf);
}

const struct iio_channels_mask *
iio_buffer_get_channels_mask(const struct iio_buffer *buf)
{
	return buf->mask;
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
