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
		(ops->get_buffer(dev, NULL, NULL, 0) != -ENOSYS);
}

struct iio_buffer * iio_device_create_buffer(const struct iio_device *dev,
		size_t samples_count)
{
	struct iio_buffer *buf;
	unsigned int sample_size = iio_device_get_sample_size(dev);
	if (!sample_size)
		return NULL;

	buf = malloc(sizeof(*buf));
	if (!buf)
		return NULL;

	buf->sample_size = sample_size;
	buf->length = buf->sample_size * samples_count;
	buf->dev = dev;
	buf->mask = calloc(dev->words, sizeof(*buf->mask));
	if (!buf->mask)
		goto err_free_buf;

	/* Set the default channel mask to the one used by the device.
	 * While input buffers will erase this as soon as the refill function
	 * is used, it is useful for output buffers, as it permits
	 * iio_buffer_foreach_sample to be used. */
	memcpy(buf->mask, dev->mask, dev->words * sizeof(*buf->mask));

	if (iio_device_open(dev, samples_count))
		goto err_free_mask;

	buf->dev_is_high_speed = device_is_high_speed(dev);
	if (buf->dev_is_high_speed) {
		/* We will use the get_buffer backend function is available.
		 * In that case, we don't need our own buffer. */
		buf->buffer = NULL;
	} else {
		buf->buffer = malloc(buf->length);
		if (!buf->buffer)
			goto err_close_device;
	}

	buf->data_length = buf->length;
	return buf;

err_close_device:
	iio_device_close(dev);
err_free_mask:
	free(buf->mask);
err_free_buf:
	free(buf);
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
		void *buf;
		read = dev->ctx->ops->get_buffer(dev, &buf,
				buffer->mask, dev->words);
		if (read >= 0)
			buffer->buffer = buf;
	} else {
		read = iio_device_read_raw(dev, buffer->buffer, buffer->length,
				buffer->mask, dev->words);
	}

	if (read >= 0)
		buffer->data_length = read;
	return read;
}

ssize_t iio_buffer_push(const struct iio_buffer *buffer)
{
	return iio_device_write_raw(buffer->dev,
			buffer->buffer, buffer->data_length);
}

static ssize_t callback_wrapper(const struct iio_channel *chn,
		void *buf, void *d)
{
	struct callback_wrapper_data *data = d;
	if (chn->index >= 0 && TEST_BIT(data->mask, chn->index))
		return data->callback(chn, buf,
				chn->format.length / 8, data->data);
	else
		return 0;
}

ssize_t iio_buffer_foreach_sample(struct iio_buffer *buffer,
		ssize_t (*callback)(const struct iio_channel *,
			void *, size_t, void *), void *d)
{
	struct callback_wrapper_data data = {
		.callback = callback,
		.data = d,
		.mask = buffer->dev->mask,
	};

	if (buffer->data_length < buffer->sample_size)
		return 0;

	return iio_device_process_samples(buffer->dev,
			buffer->mask, buffer->dev->words, buffer->buffer,
			buffer->data_length, callback_wrapper, &data);
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
	return (ptrdiff_t) iio_device_get_sample_size_mask(buffer->dev,
			buffer->mask, buffer->dev->words);
}

void * iio_buffer_end(const struct iio_buffer *buffer)
{
	return (void *) ((uintptr_t) buffer->buffer + buffer->data_length);
}
