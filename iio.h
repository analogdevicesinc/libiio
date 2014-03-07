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

#ifdef __GNUC__
#define __pure __attribute__((pure))
#else
#define __pure
#endif

#include <stdbool.h>
#include <stdlib.h>

enum iio_debug_level {
	DEBUG,
	INFO,
	WARNING,
	ERROR,
};

struct iio_context;
struct iio_device;
struct iio_channel;

/* Top-level functions */
struct iio_context * iio_create_local_context(void);
struct iio_context * iio_create_xml_context(const char *xml_file);
struct iio_context * iio_create_xml_context_mem(const char *xml, size_t len);
struct iio_context * iio_create_network_context(const char *host);
void iio_context_destroy(struct iio_context *ctx);

/* Context functions */
const char * iio_context_get_name(const struct iio_context *ctx) __pure;
unsigned int iio_context_get_devices_count(
		const struct iio_context *ctx) __pure;
struct iio_device * iio_context_get_device(const struct iio_context *ctx,
		unsigned int index);
char * iio_context_get_xml(const struct iio_context *ctx);

/* Device functions */
const char * iio_device_get_id(const struct iio_device *dev) __pure;
const char * iio_device_get_name(const struct iio_device *dev) __pure;
unsigned int iio_device_get_channels_count(
		const struct iio_device *dev) __pure;
struct iio_channel * iio_device_get_channel(const struct iio_device *dev,
		unsigned int index);
unsigned int iio_device_get_attrs_count(const struct iio_device *dev) __pure;
const char * iio_device_get_attr(const struct iio_device *dev,
		unsigned int index) __pure;

int iio_device_open(const struct iio_device *dev);
int iio_device_close(const struct iio_device *dev);

/* Channel functions */
const char * iio_channel_get_id(const struct iio_channel *chn) __pure;
const char * iio_channel_get_name(const struct iio_channel *chn) __pure;
bool iio_channel_is_output(const struct iio_channel *chn) __pure;
int iio_channel_enable(struct iio_channel *chn);
int iio_channel_disable(struct iio_channel *chn);
unsigned int iio_channel_get_attrs_count(const struct iio_channel *chn) __pure;
const char * iio_channel_get_attr(const struct iio_channel *chn,
		unsigned int index) __pure;

/* Functions to read/write the raw stream from the device */
ssize_t iio_device_read_raw(const struct iio_device *dev,
		void *dst, size_t len);
ssize_t iio_device_write_raw(const struct iio_device *dev,
		const void *src, size_t len);

/* Functions to read/write the raw stream of a channel
 * (after demux/mux process) */
ssize_t iio_channel_read_raw(const struct iio_channel *chn,
		void *dst, size_t len);
ssize_t iio_channel_write_raw(const struct iio_channel *chn,
		const void *src, size_t len);

/* Functions to read/write a stream of converted values from/to a channel */
ssize_t iio_channel_read(const struct iio_channel *chn,
		void *dst, size_t len);
ssize_t iio_channel_write(const struct iio_channel *chn,
		const void *src, size_t len);

/* Device/channel attribute functions */
ssize_t iio_device_attr_read(const struct iio_device *dev,
		const char *attr, char *dst, size_t len);
ssize_t iio_device_attr_write(const struct iio_device *dev,
		const char *attr, const char *src);
ssize_t iio_channel_attr_read(const struct iio_channel *chn,
		const char *attr, char *dst, size_t len);
ssize_t iio_channel_attr_write(const struct iio_channel *chn,
		const char *attr, const char *src);

/* Functions to set and retrieve a pointer from the device/channel structures */
void iio_device_set_data(struct iio_device *dev, void *data);
void * iio_device_get_data(const struct iio_device *dev);
void iio_channel_set_data(struct iio_channel *chn, void *data);
void * iio_channel_get_data(const struct iio_channel *chn);

/* Debug functions */
const struct iio_data_format * iio_channel_get_data_format(
		const struct iio_channel *chn);
unsigned int iio_channel_get_index(const struct iio_channel *chn);
void iio_channel_force_data_format(struct iio_channel *chn,
		const struct iio_data_format *format);
void iio_channel_set_index(struct iio_channel *chn, unsigned int index);

#endif /* __IIO_H__ */
