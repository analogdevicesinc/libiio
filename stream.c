// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2022 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "iio-private.h"

#include <errno.h>
#include <iio/iio-debug.h>
#include <stdbool.h>
#include <stdlib.h>

struct iio_stream {
	struct iio_buffer *buffer;
	struct iio_block **blocks;
	size_t nb_blocks;
	bool started, buf_enabled, all_enqueued;
	unsigned int curr;
};

struct iio_stream *
iio_buffer_create_stream(struct iio_buffer *buffer, size_t nb_blocks,
			 size_t samples_count)
{
	struct iio_stream *stream;
	size_t i, sample_size, buf_size;
	int err;

	if (!nb_blocks || !samples_count)
		return iio_ptr(-EINVAL);

	stream = zalloc(sizeof(*stream));
	if (!stream)
		return iio_ptr(-ENOMEM);

	stream->blocks = calloc(nb_blocks, sizeof(*stream->blocks));
	if (!stream->blocks) {
		err = -ENOMEM;
		goto err_free_stream;
	}

	sample_size = iio_device_get_sample_size(buffer->dev, buffer->mask);
	buf_size = samples_count * sample_size;

	for (i = 0; i < nb_blocks; i++) {
		stream->blocks[i] = iio_buffer_create_block(buffer, buf_size);
		err = iio_err(stream->blocks[i]);
		if (err) {
			stream->blocks[i] = NULL;
			goto err_free_stream_blocks;
		}
	}

	stream->buffer = buffer;
	stream->nb_blocks = nb_blocks;

	return stream;

err_free_stream_blocks:
	for (i = 0; i < nb_blocks; i++)
		if (stream->blocks[i])
			iio_block_destroy(stream->blocks[i]);
	free(stream->blocks);
err_free_stream:
	free(stream);
	return iio_ptr(err);
}

void iio_stream_destroy(struct iio_stream *stream)
{
	size_t i;

	for (i = 0; i < stream->nb_blocks; i++)
		if (stream->blocks[i])
			iio_block_destroy(stream->blocks[i]);
	free(stream->blocks);
	free(stream);
}

const struct iio_block *
iio_stream_get_next_block(struct iio_stream *stream)
{
	const struct iio_device *dev = stream->buffer->dev;
	bool is_tx = iio_device_is_tx(dev);
	unsigned int i;
	int err;

	if (!stream->started) {
		for (i = 1; !is_tx && i < stream->nb_blocks; i++) {
			err = iio_block_enqueue(stream->blocks[i], 0, false);
			if (err) {
				dev_perror(dev, err, "Unable to enqueue block");
				return iio_ptr(err);
			}
		}

		stream->started = true;

		if (is_tx)
			return stream->blocks[0];

		stream->all_enqueued = true;
	}

	err = iio_block_enqueue(stream->blocks[stream->curr], 0, false);
	if (err < 0) {
		dev_perror(dev, err, "Unable to enqueue block");
		return iio_ptr(err);
	}

	if (!stream->buf_enabled) {
		err = iio_buffer_enable(stream->buffer);
		if (err) {
			dev_perror(dev, err, "Unable to enable buffer");
			return iio_ptr(err);
		}

		stream->buf_enabled = true;
	}

	stream->curr = (stream->curr + 1) % stream->nb_blocks;

	stream->all_enqueued |= stream->curr == 0;
	if (stream->all_enqueued) {
		err = iio_block_dequeue(stream->blocks[stream->curr], false);
		if (err < 0) {
			dev_perror(dev, err, "Unable to dequeue block");
			return iio_ptr(err);
		}
	}

	return stream->blocks[stream->curr];
}
