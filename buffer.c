#include "iio-private.h"

#include <errno.h>
#include <string.h>

struct callback_wrapper_data {
	ssize_t (*callback)(const struct iio_channel *, void *, size_t, void *);
	void *data;
	uint32_t *mask;
};

static bool device_is_high_speed(const struct iio_device *dev)
{
	/* Little trick: We call the backend's get_buffer() function, which is
	 * for now only implemented in the Local backend, with a NULL pointer.
	 * It will return -ENOSYS if the device is not high speed, and either
	 * -EBADF or -EINVAL otherwise. */
	const struct iio_backend_ops *ops = dev->ctx->ops;
	return !!ops->get_buffer &&
		(ops->get_buffer(dev, NULL, 0, NULL, 0) != -ENOSYS);
}

struct iio_buffer * iio_device_create_buffer(const struct iio_device *dev,
		size_t samples_count, bool cyclic)
{
	int ret = -EINVAL;
	struct iio_buffer *buf;
	unsigned int sample_size = iio_device_get_sample_size(dev);
	if (!sample_size)
		goto err_set_errno;

	buf = malloc(sizeof(*buf));
	if (!buf) {
		ret = -ENOMEM;
		goto err_set_errno;
	}

	buf->dev_sample_size = sample_size;
	buf->length = sample_size * samples_count;
	buf->dev = dev;
	buf->mask = calloc(dev->words, sizeof(*buf->mask));
	if (!buf->mask) {
		ret = -ENOMEM;
		goto err_free_buf;
	}

	/* Set the default channel mask to the one used by the device.
	 * While input buffers will erase this as soon as the refill function
	 * is used, it is useful for output buffers, as it permits
	 * iio_buffer_foreach_sample to be used. */
	memcpy(buf->mask, dev->mask, dev->words * sizeof(*buf->mask));

	ret = iio_device_open(dev, samples_count, cyclic);
	if (ret < 0)
		goto err_free_mask;

	buf->dev_is_high_speed = device_is_high_speed(dev);
	if (buf->dev_is_high_speed) {
		/* Dequeue the first buffer, so that buf->buffer is correctly
		 * initialized */
		buf->buffer = NULL;
		if (iio_device_is_tx(dev)) {
			ret = dev->ctx->ops->get_buffer(dev, &buf->buffer,
					buf->length, buf->mask, dev->words);
			if (ret < 0)
				goto err_close_device;
		}
	} else {
		buf->buffer = malloc(buf->length);
		if (!buf->buffer) {
			ret = -ENOMEM;
			goto err_close_device;
		}
	}

	buf->sample_size = iio_device_get_sample_size_mask(dev,
			buf->mask, dev->words);
	buf->data_length = buf->length;
	return buf;

err_close_device:
	iio_device_close(dev);
err_free_mask:
	free(buf->mask);
err_free_buf:
	free(buf);
err_set_errno:
	errno = -ret;
	return NULL;
}

void iio_buffer_destroy(struct iio_buffer *buffer)
{
	iio_device_close(buffer->dev);
	if (!buffer->dev_is_high_speed)
		free(buffer->buffer);
	free(buffer->mask);
	free(buffer);
}

ssize_t iio_buffer_refill(struct iio_buffer *buffer)
{
	ssize_t read;
	const struct iio_device *dev = buffer->dev;

	if (buffer->dev_is_high_speed) {
		read = dev->ctx->ops->get_buffer(dev, &buffer->buffer,
				buffer->length, buffer->mask, dev->words);
	} else {
		read = iio_device_read_raw(dev, buffer->buffer, buffer->length,
				buffer->mask, dev->words);
	}

	if (read >= 0) {
		buffer->data_length = read;
		buffer->sample_size = iio_device_get_sample_size_mask(dev,
				buffer->mask, dev->words);
	}
	return read;
}

ssize_t iio_buffer_push(struct iio_buffer *buffer)
{
	const struct iio_device *dev = buffer->dev;

	if (buffer->dev_is_high_speed) {
		void *buf;
		ssize_t ret = dev->ctx->ops->get_buffer(dev,
				&buf, buffer->length, buffer->mask, dev->words);
		if (ret >= 0)
			buffer->buffer = buf;
		return ret;
	} else {
		size_t length = buffer->length;
		void *ptr = buffer->buffer;

		/* iio_device_write_raw doesn't guarantee that all bytes are
		 * written */
		while (length) {
			ssize_t ret = iio_device_write_raw(dev, ptr, length);
			if (ret < 0)
				return ret;

			length -= ret;
			ptr = (void *) ((uintptr_t) ptr + ret);
		}

		return (ssize_t) buffer->length;
	}
}

ssize_t iio_buffer_foreach_sample(struct iio_buffer *buffer,
		ssize_t (*callback)(const struct iio_channel *,
			void *, size_t, void *), void *d)
{
	uintptr_t ptr = (uintptr_t) buffer->buffer,
		  end = ptr + buffer->data_length;
	const struct iio_device *dev = buffer->dev;
	ssize_t processed = 0;

	if (buffer->sample_size <= 0)
		return -EINVAL;

	if (buffer->data_length < buffer->dev_sample_size)
		return 0;

	while (end - ptr >= (size_t) buffer->sample_size) {
		unsigned int i;

		for (i = 0; i < dev->nb_channels; i++) {
			const struct iio_channel *chn = dev->channels[i];
			unsigned int length = chn->format.length / 8;

			if (chn->index < 0)
				break;

			/* Test if the buffer has samples for this channel */
			if (!TEST_BIT(buffer->mask, chn->index))
				continue;

			if (ptr % length)
				ptr += length - (ptr % length);

			/* Test if the client wants samples from this channel */
			if (TEST_BIT(dev->mask, chn->index)) {
				ssize_t ret = callback(chn,
						(void *) ptr, length, d);
				if (ret < 0)
					return ret;
				else
					processed += ret;
			}

			ptr += length;
		}
	}
	return processed;
}

void * iio_buffer_start(const struct iio_buffer *buffer)
{
	return buffer->buffer;
}

void * iio_buffer_first(const struct iio_buffer *buffer,
		const struct iio_channel *chn)
{
	size_t len;
	unsigned int i;
	uintptr_t ptr = (uintptr_t) buffer->buffer;

	if (!iio_channel_is_enabled(chn))
		return iio_buffer_end(buffer);

	for (i = 0; i < buffer->dev->nb_channels; i++) {
		struct iio_channel *cur = buffer->dev->channels[i];
		len = cur->format.length / 8;

		/* NOTE: dev->channels are ordered by index */
		if (cur->index < 0 || cur->index == chn->index)
			break;

		if (ptr % len)
			ptr += len - (ptr % len);
		ptr += len;
	}

	len = chn->format.length / 8;
	if (ptr % len)
		ptr += len - (ptr % len);
	return (void *) ptr;
}

ptrdiff_t iio_buffer_step(const struct iio_buffer *buffer)
{
	return (ptrdiff_t) buffer->sample_size;
}

void * iio_buffer_end(const struct iio_buffer *buffer)
{
	return (void *) ((uintptr_t) buffer->buffer + buffer->data_length);
}

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
