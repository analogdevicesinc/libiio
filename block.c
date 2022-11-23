// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2022 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "iio-private.h"

#include <errno.h>
#include <iio/iio-lock.h>
#include <stdbool.h>
#include <string.h>

struct iio_block {
	struct iio_buffer *buffer;
	struct iio_block_pdata *pdata;
	size_t size;
	void *data;

	struct iio_task_token *token, *old_token;
	size_t bytes_used;
	bool cyclic;
};

struct iio_block *
iio_buffer_create_block(struct iio_buffer *buf, size_t size)
{
	const struct iio_device *dev = buf->dev;
	const struct iio_backend_ops *ops = dev->ctx->ops;
	struct iio_block_pdata *pdata;
	size_t sample_size;
	struct iio_block *block;
	int ret;

	sample_size = iio_device_get_sample_size(dev, buf->mask);
	if (sample_size == 0 || size < sample_size)
		return iio_ptr(-EINVAL);

	block = zalloc(sizeof(*block));
	if (!block)
		return iio_ptr(-ENOMEM);

	if (ops->create_block) {
		pdata = ops->create_block(buf->pdata, size, &block->data);
		ret = iio_err(pdata);
		if (!ret)
			block->pdata = pdata;
		else if (ret != -ENOSYS)
			goto err_free_block;
	}

	if (!block->pdata) {
		block->data = malloc(size);
		if (!block->data) {
			ret = -ENOMEM;
			goto err_free_block;
		}

		if (size > buf->length)
		      buf->length = size;

		buf->block_size = size;
	}

	block->buffer = buf;
	block->size = size;

	iio_mutex_lock(buf->lock);
	buf->nb_blocks++;
	iio_mutex_unlock(buf->lock);

	return block;

err_free_block:
	free(block);
	return iio_ptr(ret);
}

void iio_block_destroy(struct iio_block *block)
{
	struct iio_buffer *buf = block->buffer;
	const struct iio_backend_ops *ops = buf->dev->ctx->ops;

	/* Stop the cyclic task */
	block->cyclic = false;

	if (block->token) {
		iio_task_cancel(block->token);
		iio_task_sync(block->token, 0);
	}
	if (ops->free_block && block->pdata)
		ops->free_block(block->pdata);
	else
		free(block->data);
	free(block);

	iio_mutex_lock(buf->lock);
	buf->nb_blocks--;
	iio_mutex_unlock(buf->lock);
}

static int iio_block_write(struct iio_block *block)
{
	const struct iio_backend_ops *ops = block->buffer->dev->ctx->ops;
	size_t bytes_used = block->bytes_used;
	ssize_t ret;

	if (!ops->writebuf)
		return -ENOSYS;

	ret = ops->writebuf(block->buffer->pdata, block->data, bytes_used);
	return ret < 0 ? (int) ret : 0;
}

static int iio_block_read(struct iio_block *block)
{
	const struct iio_backend_ops *ops = block->buffer->dev->ctx->ops;
	size_t bytes_used = block->bytes_used;
	ssize_t ret;

	if (!ops->readbuf)
		return -ENOSYS;

	ret = ops->readbuf(block->buffer->pdata, block->data, bytes_used);
	return ret < 0 ? (int) ret : 0;
}

int iio_block_io(struct iio_block *block)
{
	if (!iio_device_is_tx(block->buffer->dev))
		return iio_block_read(block);

	if (block->old_token)
		iio_task_sync(block->old_token, 0);

	if (block->cyclic) {
		block->old_token = block->token;
		block->token = iio_task_enqueue(block->buffer->worker, block);
	}

	return iio_block_write(block);
}

int iio_block_enqueue(struct iio_block *block, size_t bytes_used, bool cyclic)
{
	const struct iio_buffer *buffer = block->buffer;
	const struct iio_device *dev = buffer->dev;
	const struct iio_backend_ops *ops = dev->ctx->ops;

	if (bytes_used > block->size)
		return -EINVAL;

	if (!bytes_used)
		bytes_used = block->size;

	if (ops->enqueue_block && block->pdata)
		return ops->enqueue_block(block->pdata, bytes_used, cyclic);

	if (block->token) {
		/* Already enqueued */
		return -EPERM;
	}

	block->bytes_used = bytes_used;
	block->cyclic = cyclic;
	block->token = iio_task_enqueue(buffer->worker, block);

	return iio_err(block->token);
}

int iio_block_dequeue(struct iio_block *block, bool nonblock)
{
	struct iio_buffer *buffer = block->buffer;
	const struct iio_backend_ops *ops = buffer->dev->ctx->ops;
	struct iio_task_token *token;

	if (ops->dequeue_block && block->pdata)
		return ops->dequeue_block(block->pdata, nonblock);

	iio_mutex_lock(buffer->lock);
	token = block->token;

	if (nonblock && token && !iio_task_is_done(token)) {
		iio_mutex_unlock(buffer->lock);
		return -EBUSY;
	}

	block->token = NULL;
	iio_mutex_unlock(buffer->lock);

	if (!token) {
		/* Already dequeued */
		return -EPERM;
	}

	return iio_task_sync(token, 0);
}

void *iio_block_start(const struct iio_block *block)
{
	return block->data;
}

void *iio_block_end(const struct iio_block *block)
{
	return (void *) ((uintptr_t) block->data + block->size);
}

void *iio_block_first(const struct iio_block *block,
		      const struct iio_channel *chn)
{
	uintptr_t ptr = (uintptr_t)block->data, start = ptr;
	const struct iio_device *dev = block->buffer->dev;
	const struct iio_buffer *buf = block->buffer;
	const struct iio_channel *cur;
	unsigned int i;
	size_t len;

	/* Test if the block has samples for this channel */
	if (!iio_channels_mask_test_bit(buf->mask, chn->number))
		return iio_block_end(block);

	for (i = 0; i < dev->nb_channels; i++) {
		cur = dev->channels[i];
		len = cur->format.length / 8 * cur->format.repeat;

		/* NOTE: dev->channels are ordered by index */
		if (cur->index < 0 || cur->index == chn->index)
			break;

		if (!iio_channels_mask_test_bit(buf->mask, cur->number))
			continue;

		/* Two channels with the same index use the same samples */
		if (i > 0 && cur->index == dev->channels[i - 1]->index)
			continue;

		if ((ptr - start) % len)
			ptr += len - ((ptr - start) % len);
		ptr += len;
	}

	len = chn->format.length / 8;
	if ((ptr - start) % len)
		ptr += len - ((ptr - start) % len);

	return (void *) ptr;
}

ssize_t
iio_block_foreach_sample(const struct iio_block *block,
			 const struct iio_channels_mask *mask,
			 ssize_t (*callback)(const struct iio_channel *,
					     void *, size_t, void *), void *d)
{
	uintptr_t ptr = (uintptr_t) block->data,
		  start = ptr,
		  end = ptr + block->size;
	const struct iio_buffer *buf = block->buffer;
	const struct iio_device *dev = buf->dev;
	size_t sample_size;
	ssize_t processed = 0;

	sample_size = iio_device_get_sample_size(dev, buf->mask);
	if (sample_size == 0)
		return -EINVAL;

	while (end - ptr >= (size_t) sample_size) {
		unsigned int i;

		for (i = 0; i < dev->nb_channels; i++) {
			const struct iio_channel *chn = dev->channels[i];
			unsigned int length = chn->format.length / 8;

			if (chn->index < 0)
				break;

			/* Test if the block has samples for this channel */
			if (!iio_channels_mask_test_bit(buf->mask, chn->number))
				continue;

			if ((ptr - start) % length)
				ptr += length - ((ptr - start) % length);

			/* Test if the client wants samples from this channel */
			if (iio_channels_mask_test_bit(mask, chn->number)) {
				ssize_t ret = callback(chn,
						(void *) ptr, length, d);
				if (ret < 0)
					return ret;
				else
					processed += ret;
			}

			if (i == dev->nb_channels - 1 || dev->channels[
					i + 1]->index != chn->index)
				ptr += length * chn->format.repeat;
		}
	}

	return processed;
}

struct iio_buffer * iio_block_get_buffer(const struct iio_block *block)
{
	return block->buffer;
}
