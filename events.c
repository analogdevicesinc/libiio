// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2023 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "iio-private.h"

#include <ctype.h>
#include <errno.h>
#include <iio/iio-debug.h>

struct iio_event_stream_pdata;

struct iio_event_stream {
	const struct iio_device *dev;
	struct iio_event_stream_pdata *pdata;
};

/* Corresponds to IIO_EVENT_CODE_EXTRACT_CHAN() and
 * IIO_EVENT_CODE_EXTRACT_CHAN2() macros of <linux/iio/events.h> */
static inline int16_t
iio_event_get_channel_id(const struct iio_event *event, unsigned int channel)
{
	return (int16_t)(event->id >> (channel << 4));
}

/* Corresponds to IIO_EVENT_CODE_EXTRACT_DIFF() of <linux/iio/events.h> */
static inline bool
iio_event_is_differential(const struct iio_event *event)
{
	return event->id & BIT(55);
}

/* Corresponds to IIO_EVENT_CODE_EXTRACT_MODIFIER() of <linux/iio/events.h> */
static inline enum iio_modifier
iio_event_get_modifier(const struct iio_event *event)
{
	return (enum iio_modifier)((event->id >> 40) & 0xff);
}

/* Corresponds to IIO_EVENT_CODE_EXTRACT_CHAN_TYPE() of <linux/iio/events.h> */
static inline enum iio_chan_type
iio_event_get_chan_type(const struct iio_event *event)
{
	return (enum iio_chan_type)((event->id >> 32) & 0xff);
}

const struct iio_channel *
iio_event_get_channel(const struct iio_event *event,
		      const struct iio_device *dev, bool diff)
{
	const struct iio_channel *chn = NULL;
	const char *ptr;
	unsigned int i;
	int16_t chid;

	if (diff && !iio_event_is_differential(event))
		return NULL;

	chid = iio_event_get_channel_id(event, diff);
	if (chid < 0)
		return NULL;

	if ((unsigned int)chid >= dev->nb_channels) {
		dev_warn(dev, "Unexpected IIO event channel ID\n");
		return NULL;
	}

	for (i = 0; i < dev->nb_channels; i++) {
		chn = dev->channels[i];

		if (chn->type != iio_event_get_chan_type(event)
		    || chn->modifier != iio_event_get_modifier(event)) {
			continue;
		}

		for (ptr = chn->id; *ptr && isalpha((unsigned char)*ptr); )
			ptr++;

		if (!*ptr && chid <= 0)
			break;

		if ((uint16_t)chid == strtoul(ptr, NULL, 10))
			break;
	}

	if (chn) {
		chn_dbg(chn, "Found channel %s for event\n",
			iio_channel_get_id(chn));
	} else {
		dev_dbg(dev, "Unable to find channel for event\n");
	}

	return chn;
}

struct iio_event_stream *
iio_device_create_event_stream(const struct iio_device *dev)
{
	struct iio_event_stream *stream;
	int err;

	if (!dev->ctx->ops->open_ev)
		return iio_ptr(-ENOSYS);

	stream = zalloc(sizeof(*stream));
	if (!stream)
		return iio_ptr(-ENOMEM);

	stream->dev = dev;

	stream->pdata = dev->ctx->ops->open_ev(dev);
	err = iio_err(stream->pdata);
	if (err) {
		free(stream);
		return iio_ptr(err);
	}

	return stream;
}

void iio_event_stream_destroy(struct iio_event_stream *stream)
{
	if (stream->dev->ctx->ops->close_ev)
		stream->dev->ctx->ops->close_ev(stream->pdata);

	free(stream);
}

int iio_event_stream_read(struct iio_event_stream *stream,
			  struct iio_event *out_event,
			  bool nonblock)
{
	if (!stream->dev->ctx->ops->read_ev)
		return -ENOSYS;

	if (!out_event)
		return -EINVAL;

	return stream->dev->ctx->ops->read_ev(stream->pdata, out_event, nonblock);
}
