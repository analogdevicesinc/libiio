#include "iio-private.h"

struct callback_wrapper_data {
	ssize_t (*callback)(const struct iio_channel *, void *, size_t, void *);
	void *data;
	uint32_t *mask;
};

struct iio_buffer * iio_device_create_buffer(const struct iio_device *dev,
		size_t length)
{
	unsigned int i;
	struct iio_buffer *buf = malloc(sizeof(*buf));
	if (!buf)
		return NULL;

	buf->data_length = 0;
	buf->length = length;
	buf->dev = dev;
	buf->words = (dev->nb_channels + 31) / 32;
	buf->mask = calloc(buf->words, sizeof(*buf->mask));
	if (!buf->mask)
		goto err_free_buf;
	buf->open_mask = calloc(buf->words, sizeof(*buf->open_mask));
	if (!buf->open_mask)
		goto err_free_mask;

	for (i = 0; i < dev->nb_channels; i++) {
		struct iio_channel *chn = dev->channels[i];
		if (iio_channel_is_enabled(chn) && chn->index >= 0)
			SET_BIT(buf->open_mask, chn->index);
	}

	buf->sample_size = iio_device_get_sample_size(dev,
			buf->open_mask, buf->words);

	buf->buffer = malloc(length);
	if (buf->buffer)
		return buf;

	free(buf->open_mask);
err_free_mask:
	free(buf->mask);
err_free_buf:
	free(buf);
	return NULL;
}

void iio_buffer_destroy(struct iio_buffer *buffer)
{
	free(buffer->buffer);
	free(buffer->open_mask);
	free(buffer->mask);
	free(buffer);
}

int iio_buffer_refill(struct iio_buffer *buffer)
{
	ssize_t read = iio_device_read_raw(buffer->dev,
			buffer->buffer, buffer->length,
			buffer->mask, buffer->words);
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
		.mask = buffer->open_mask,
	};

	if (buffer->data_length < buffer->sample_size)
		return 0;

	return iio_device_process_samples(buffer->dev,
			buffer->mask, buffer->words, buffer->buffer,
			buffer->data_length, callback_wrapper, &data);
}
