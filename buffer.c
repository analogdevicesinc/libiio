#include "iio-private.h"

struct callback_wrapper_data {
	ssize_t (*callback)(const struct iio_channel *, void *, size_t, void *);
	void *data;
	uint32_t *mask;
};

struct iio_buffer * iio_device_create_buffer(const struct iio_device *dev,
		size_t samples_count)
{
	struct iio_buffer *buf = malloc(sizeof(*buf));
	if (!buf)
		return NULL;

	buf->data_length = 0;
	buf->sample_size = iio_device_get_sample_size(dev);
	buf->length = buf->sample_size * samples_count;
	buf->dev = dev;
	buf->mask = calloc(dev->words, sizeof(*buf->mask));
	if (!buf->mask)
		goto err_free_buf;

	buf->buffer = malloc(buf->length);
	if (buf->buffer)
		return buf;

	free(buf->mask);
err_free_buf:
	free(buf);
	return NULL;
}

void iio_buffer_destroy(struct iio_buffer *buffer)
{
	free(buffer->buffer);
	free(buffer->mask);
	free(buffer);
}

int iio_buffer_refill(struct iio_buffer *buffer)
{
	ssize_t read = iio_device_read_raw(buffer->dev,
			buffer->buffer, buffer->length,
			buffer->mask, buffer->dev->words);
	if (read < 0)
		return (int) read;

	buffer->data_length = read;
	return 0;
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

void * iio_buffer_first(const struct iio_buffer *buffer,
		const struct iio_channel *chn)
{
	size_t len;
	unsigned int i;
	void *ptr = buffer->buffer;

	if (!iio_channel_is_enabled(chn))
		return iio_buffer_end(buffer);

	for (i = 0; i < buffer->dev->nb_channels; i++) {
		struct iio_channel *cur = buffer->dev->channels[i];
		len = cur->format.length / 8;

		/* NOTE: dev->channels are ordered by index */
		if (cur->index < 0 || cur->index == chn->index)
			break;

		if ((uintptr_t) ptr % len)
			ptr += len - ((uintptr_t) ptr % len);
		ptr += len;
	}

	len = chn->format.length / 8;
	if ((uintptr_t) ptr % len)
		ptr += len - ((uintptr_t) ptr % len);
	return ptr;
}

ptrdiff_t iio_buffer_step(const struct iio_buffer *buffer,
		const struct iio_channel *chn)
{
	return (ptrdiff_t) iio_device_get_sample_size_mask(buffer->dev,
			buffer->mask, buffer->dev->words);
}

void * iio_buffer_end(const struct iio_buffer *buffer)
{
	return buffer->buffer + buffer->data_length;
}
