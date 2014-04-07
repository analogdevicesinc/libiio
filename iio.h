/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * */

#ifndef __IIO_H__
#define __IIO_H__

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <stddef.h>

#ifdef _MSC_BUILD
/* Come on Microsoft, time to get some C99... */
typedef long ssize_t;
#endif

#ifdef __GNUC__
#define __cnst __attribute__((const))
#define __pure __attribute__((pure))
#else
#define __cnst
#define __pure
#endif

#ifdef _WIN32
#   ifdef LIBIIO_EXPORTS
#	define __api __declspec(dllexport)
#   else
#	define __api __declspec(dllimport)
#   endif
#elif __GNUC__ >= 4
#   define __api __attribute__((visibility ("default")))
#else
#   define __api
#endif

struct iio_data_format {
	unsigned int length;
	unsigned int bits;
	unsigned int shift;
	bool with_scale, is_signed, is_be;
	double scale;
};

struct iio_context;
struct iio_device;
struct iio_channel;
struct iio_buffer;

/* Top-level functions */
__api struct iio_context * iio_create_local_context(void);
__api struct iio_context * iio_create_xml_context(const char *xml_file);
__api struct iio_context * iio_create_xml_context_mem(
		const char *xml, size_t len);
__api struct iio_context * iio_create_network_context(const char *host);
__api void iio_context_destroy(struct iio_context *ctx);

/* Context functions */
__api __pure const char * iio_context_get_name(const struct iio_context *ctx);
__api __pure unsigned int iio_context_get_devices_count(
		const struct iio_context *ctx);
__api __pure struct iio_device * iio_context_get_device(
		const struct iio_context *ctx, unsigned int index);
__api __pure struct iio_device * iio_context_find_device(
		const struct iio_context *ctx, const char *name);
__api __pure const char * iio_context_get_xml(const struct iio_context *ctx);

/* Device functions */
__api __pure const char * iio_device_get_id(const struct iio_device *dev);
__api __pure const char * iio_device_get_name(const struct iio_device *dev);
__api __pure unsigned int iio_device_get_channels_count(
		const struct iio_device *dev);
__api __pure struct iio_channel * iio_device_get_channel(
		const struct iio_device *dev, unsigned int index);
__api __pure struct iio_channel * iio_device_find_channel(
		const struct iio_device *dev, const char *name, bool output);
__api __pure unsigned int iio_device_get_attrs_count(
		const struct iio_device *dev);
__api __pure const char * iio_device_get_attr(
		const struct iio_device *dev, unsigned int index);
__api __pure const char * iio_device_find_attr(
		const struct iio_device *dev, const char *name);
__api int iio_device_get_trigger(const struct iio_device *dev,
		const struct iio_device **trigger);
__api int iio_device_set_trigger(const struct iio_device *dev,
		const struct iio_device *trigger);

__api int iio_device_open(const struct iio_device *dev, size_t samples_count);
__api int iio_device_close(const struct iio_device *dev);

__api ssize_t iio_device_get_sample_size(const struct iio_device *dev);

/* Channel functions */
__api __pure const char * iio_channel_get_id(const struct iio_channel *chn);
__api __pure const char * iio_channel_get_name(const struct iio_channel *chn);
__api __pure bool iio_channel_is_output(const struct iio_channel *chn);
__api bool iio_channel_is_enabled(const struct iio_channel *chn);
__api void iio_channel_enable(struct iio_channel *chn);
__api void iio_channel_disable(struct iio_channel *chn);
__api __pure unsigned int iio_channel_get_attrs_count(
		const struct iio_channel *chn);
__api __pure const char * iio_channel_get_attr(
		const struct iio_channel *chn, unsigned int index);
__api __pure const char * iio_channel_find_attr(
		const struct iio_channel *chn, const char *name);
__api __pure long iio_channel_get_index(const struct iio_channel *chn);
__api __cnst const struct iio_data_format * iio_channel_get_data_format(
		const struct iio_channel *chn);

/* Convert the sample pointed by 'src' to 'dst' */
__api void iio_channel_convert(const struct iio_channel *chn,
		void *dst, const void *src);

/* Trigger functions */
__api __pure bool iio_device_is_trigger(const struct iio_device *dev);
__api int iio_trigger_get_rate(const struct iio_device *trigger,
		unsigned long *rate);
__api int iio_trigger_set_rate(const struct iio_device *trigger,
		unsigned long rate);

/* Functions to read/write the raw stream from the device */
__api ssize_t iio_device_read_raw(const struct iio_device *dev,
		void *dst, size_t len, uint32_t *mask, size_t words);
__api ssize_t iio_device_write_raw(const struct iio_device *dev,
		const void *src, size_t len);

/* Buffer functions */
__api struct iio_buffer * iio_device_create_buffer(const struct iio_device *dev,
		size_t samples_count, bool is_output);
__api void iio_buffer_destroy(struct iio_buffer *buf);

__api ssize_t iio_buffer_refill(struct iio_buffer *buf);
__api int iio_buffer_push(const struct iio_buffer *buf);

__api ssize_t iio_buffer_foreach_sample(struct iio_buffer *buf,
		ssize_t (*callback)(const struct iio_channel *,
			void *, size_t, void *), void *data);
__api void * iio_buffer_first(const struct iio_buffer *buffer,
		const struct iio_channel *chn);
__api ptrdiff_t iio_buffer_step(const struct iio_buffer *buffer);
__api void * iio_buffer_end(const struct iio_buffer *buffer);

/* Functions to read/write the raw stream of a channel
 * (after demux/mux process) */
__api size_t iio_channel_read_raw(const struct iio_channel *chn,
		struct iio_buffer *buffer, void *dst, size_t len);
__api size_t iio_channel_write_raw(const struct iio_channel *chn,
		struct iio_buffer *buffer, const void *src, size_t len);

/* Functions to read/write a stream of converted values from/to a channel */
__api ssize_t iio_channel_read(const struct iio_channel *chn,
		void *dst, size_t len);
__api ssize_t iio_channel_write(const struct iio_channel *chn,
		const void *src, size_t len);

/* Device/channel attribute functions */
__api ssize_t iio_device_attr_read(const struct iio_device *dev,
		const char *attr, char *dst, size_t len);
__api ssize_t iio_device_attr_write(const struct iio_device *dev,
		const char *attr, const char *src);
__api ssize_t iio_channel_attr_read(const struct iio_channel *chn,
		const char *attr, char *dst, size_t len);
__api ssize_t iio_channel_attr_write(const struct iio_channel *chn,
		const char *attr, const char *src);

/* Functions to set and retrieve a pointer from the device/channel structures */
__api void iio_device_set_data(struct iio_device *dev, void *data);
__api void * iio_device_get_data(const struct iio_device *dev);
__api void iio_channel_set_data(struct iio_channel *chn, void *data);
__api void * iio_channel_get_data(const struct iio_channel *chn);

/* Top-level functions to read/write attributes */
__api int iio_device_attr_read_bool(const struct iio_device *dev,
		const char *attr, bool *val);
__api int iio_device_attr_read_longlong(const struct iio_device *dev,
		const char *attr, long long *val);
__api int iio_device_attr_read_double(const struct iio_device *dev,
		const char *attr, double *val);

__api int iio_device_attr_write_bool(const struct iio_device *dev,
		const char *attr, bool val);
__api int iio_device_attr_write_longlong(const struct iio_device *dev,
		const char *attr, long long val);
__api int iio_device_attr_write_double(const struct iio_device *dev,
		const char *attr, double val);

__api int iio_channel_attr_read_bool(const struct iio_channel *chn,
		const char *attr, bool *val);
__api int iio_channel_attr_read_longlong(const struct iio_channel *chn,
		const char *attr, long long *val);
__api int iio_channel_attr_read_double(const struct iio_channel *chn,
		const char *attr, double *val);

__api int iio_channel_attr_write_bool(const struct iio_channel *chn,
		const char *attr, bool val);
__api int iio_channel_attr_write_longlong(const struct iio_channel *chn,
		const char *attr, long long val);
__api int iio_channel_attr_write_double(const struct iio_channel *chn,
		const char *attr, double val);

#endif /* __IIO_H__ */
