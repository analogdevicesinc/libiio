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

#ifdef __cplusplus
extern "C" {
#endif

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


/* ------------------------- Context functions -------------------------------*/

/* Create a context from local IIO devices (Linux only). */
__api struct iio_context * iio_create_local_context(void);

/* Create a context from a XML file or XML data in memory.
 * The format of the XML must comply to the one returned by
 * iio_context_get_xml(). */
__api struct iio_context * iio_create_xml_context(const char *xml_file);
__api struct iio_context * iio_create_xml_context_mem(
		const char *xml, size_t len);

/* Create a context from the network. The "host" parameter corresponds to
 * the hostname, IPv4 or IPv6 address where the IIO Daemon is running. */
__api struct iio_context * iio_create_network_context(const char *host);

/* Destroy the given context.
 * After that function, the iio_context pointer shall be invalid. */
__api void iio_context_destroy(struct iio_context *ctx);

/* Returns a XML representation of the current IIO context. */
__api __pure const char * iio_context_get_xml(const struct iio_context *ctx);

/* Returns the name of the context. */
__api __pure const char * iio_context_get_name(const struct iio_context *ctx);

/* Functions to get the number of devices, and pointers to their structure. */
__api __pure unsigned int iio_context_get_devices_count(
		const struct iio_context *ctx);
__api __pure struct iio_device * iio_context_get_device(
		const struct iio_context *ctx, unsigned int index);

/* Finds a device structure by its name of ID. */
__api __pure struct iio_device * iio_context_find_device(
		const struct iio_context *ctx, const char *name);


/* ------------------------- Device functions --------------------------------*/

/* Retrieve the device's ID (e.g. iio:device0) or name (e.g. xadc).
 * Note that the returned ID will never be NULL, but the name can be. */
__api __pure const char * iio_device_get_id(const struct iio_device *dev);
__api __pure const char * iio_device_get_name(const struct iio_device *dev);

/* Get the number of channels and attributes. */
__api __pure unsigned int iio_device_get_channels_count(
		const struct iio_device *dev);
__api __pure unsigned int iio_device_get_attrs_count(
		const struct iio_device *dev);

/* Retrieve a channel structure or device attribute from index. */
__api __pure struct iio_channel * iio_device_get_channel(
		const struct iio_device *dev, unsigned int index);
__api __pure const char * iio_device_get_attr(
		const struct iio_device *dev, unsigned int index);

/* Find a channel or attribute of a device by name or ID. */
__api __pure struct iio_channel * iio_device_find_channel(
		const struct iio_device *dev, const char *name, bool output);
__api __pure const char * iio_device_find_attr(
		const struct iio_device *dev, const char *name);

/* Functions to read the content of a device attribute. */
__api ssize_t iio_device_attr_read(const struct iio_device *dev,
		const char *attr, char *dst, size_t len);
__api int iio_device_attr_read_bool(const struct iio_device *dev,
		const char *attr, bool *val);
__api int iio_device_attr_read_longlong(const struct iio_device *dev,
		const char *attr, long long *val);
__api int iio_device_attr_read_double(const struct iio_device *dev,
		const char *attr, double *val);

/* Functions to write the content of a device attribute. */
__api ssize_t iio_device_attr_write(const struct iio_device *dev,
		const char *attr, const char *src);
__api int iio_device_attr_write_bool(const struct iio_device *dev,
		const char *attr, bool val);
__api int iio_device_attr_write_longlong(const struct iio_device *dev,
		const char *attr, long long val);
__api int iio_device_attr_write_double(const struct iio_device *dev,
		const char *attr, double val);

/* Associate/retrieve a pointer to an iio_device structure. */
__api void iio_device_set_data(struct iio_device *dev, void *data);
__api void * iio_device_get_data(const struct iio_device *dev);


/* ------------------------- Trigger functions -------------------------------*/

/* Associate/retrieve a device structure corresponding to the trigger to use. */
__api int iio_device_get_trigger(const struct iio_device *dev,
		const struct iio_device **trigger);
__api int iio_device_set_trigger(const struct iio_device *dev,
		const struct iio_device *trigger);

/* Returns true if the given device is a trigger, false otherwise. */
__api __pure bool iio_device_is_trigger(const struct iio_device *dev);

/* Set or get the trigger frequency. The iio_device passed must be a valid
 * trigger, or an error will be thrown. */
__api int iio_trigger_get_rate(const struct iio_device *trigger,
		unsigned long *rate);
__api int iio_trigger_set_rate(const struct iio_device *trigger,
		unsigned long rate);


/* ------------------------- Channel functions -------------------------------*/

/* Retrieve the channel's ID (e.g. voltage0) or name (e.g. vccint).
 * Note that the returned ID will never be NULL, but the name can be. */
__api __pure const char * iio_channel_get_id(const struct iio_channel *chn);
__api __pure const char * iio_channel_get_name(const struct iio_channel *chn);

/* Returns true if the channel is an output channel, false otherwise. */
__api __pure bool iio_channel_is_output(const struct iio_channel *chn);

/* Get the number of attributes. */
__api __pure unsigned int iio_channel_get_attrs_count(
		const struct iio_channel *chn);

/* Get an attribute by its index. */
__api __pure const char * iio_channel_get_attr(
		const struct iio_channel *chn, unsigned int index);

/* Search for an attribute by its name. If not found, NULL is returned.
 * Otherwise, a pointer to the attribute string (and not to the "name" string)
 * is returned. */
__api __pure const char * iio_channel_find_attr(
		const struct iio_channel *chn, const char *name);

/* Functions to read the content of a channel attribute. */
__api ssize_t iio_channel_attr_read(const struct iio_channel *chn,
		const char *attr, char *dst, size_t len);
__api int iio_channel_attr_read_bool(const struct iio_channel *chn,
		const char *attr, bool *val);
__api int iio_channel_attr_read_longlong(const struct iio_channel *chn,
		const char *attr, long long *val);
__api int iio_channel_attr_read_double(const struct iio_channel *chn,
		const char *attr, double *val);

/* Functions to write the content of a channel attribute. */
__api ssize_t iio_channel_attr_write(const struct iio_channel *chn,
		const char *attr, const char *src);
__api int iio_channel_attr_write_bool(const struct iio_channel *chn,
		const char *attr, bool val);
__api int iio_channel_attr_write_longlong(const struct iio_channel *chn,
		const char *attr, long long val);
__api int iio_channel_attr_write_double(const struct iio_channel *chn,
		const char *attr, double val);

/* Enable/disable the given channel. Before creating an input or output buffer,
 * the channels to read or write must be enabled. */
__api void iio_channel_enable(struct iio_channel *chn);
__api void iio_channel_disable(struct iio_channel *chn);
__api bool iio_channel_is_enabled(const struct iio_channel *chn);

/* Demultiplex the samples corresponding of the given channel from the
 * input buffer, and store them sequentially at the address pointed by "dst".
 * The iio_channel_read function will also convert the samples from hardware
 * format to host format. */
__api size_t iio_channel_read_raw(const struct iio_channel *chn,
		struct iio_buffer *buffer, void *dst, size_t len);
__api size_t iio_channel_read(const struct iio_channel *chn,
		struct iio_buffer *buffer, void *dst, size_t len);


/* Multiplex the samples read at the address pointed by "src" to the output
 * buffer. The iio_channel_write function will also convert the samples from
 * host format to hardware format. */
__api size_t iio_channel_write_raw(const struct iio_channel *chn,
		struct iio_buffer *buffer, const void *src, size_t len);
__api size_t iio_channel_write(const struct iio_channel *chn,
		struct iio_buffer *buffer, const void *src, size_t len);

/* Associate/retrieve a pointer to an iio_channel structure. */
__api void iio_channel_set_data(struct iio_channel *chn, void *data);
__api void * iio_channel_get_data(const struct iio_channel *chn);


/* ------------------------- Buffer functions --------------------------------*/

/* Create an input or output buffer from the given device, with the
 * specified size. Channels that have to be read or written must be enabled
 * before creating the buffer. */
__api struct iio_buffer * iio_device_create_buffer(const struct iio_device *dev,
		size_t samples_count, bool is_output);

/* Destroy the buffer (closes the device, frees the memory).
 * After that function, the iio_buffer pointer shall be invalid. */
__api void iio_buffer_destroy(struct iio_buffer *buf);

/* Refill the input buffer (fetch more samples from the hardware). */
__api ssize_t iio_buffer_refill(struct iio_buffer *buf);

/* Send the output buffer to the hardware. */
__api int iio_buffer_push(const struct iio_buffer *buf);

/* Those three functions can be used to iterate over the samples of one channel
 * in the buffer, doing the following:
 *
 * for (void *ptr = iio_buffer_first(buffer, chn);
 *	    ptr < iio_buffer_end(buffer);
 *	    ptr += iio_buffer_step(buffer)) {
 *	    ....
 * }
 */
__api void * iio_buffer_first(const struct iio_buffer *buffer,
		const struct iio_channel *chn);
__api ptrdiff_t iio_buffer_step(const struct iio_buffer *buffer);
__api void * iio_buffer_end(const struct iio_buffer *buffer);

/* This function takes a callback as parameter, that will be called for each
 * sample of the given channel present in the buffer. */
__api ssize_t iio_buffer_foreach_sample(struct iio_buffer *buf,
		ssize_t (*callback)(const struct iio_channel *chn,
			void *src, size_t bytes, void *d), void *data);



/* ------------------------- Low-level functions -----------------------------*/

/* Open/close a device. This is not required when using the iio_buffer
 * functions; it is only useful when used with iio_device_read_raw /
 * iio_device_write_raw. */
__api int iio_device_open(const struct iio_device *dev, size_t samples_count);
__api int iio_device_close(const struct iio_device *dev);

/* Returns the current sample size (in bytes). */
__api ssize_t iio_device_get_sample_size(const struct iio_device *dev);

/* Returns the index of the given channel. */
__api __pure long iio_channel_get_index(const struct iio_channel *chn);

/* Returns a pointer to a channel's data format structure. */
__api __cnst const struct iio_data_format * iio_channel_get_data_format(
		const struct iio_channel *chn);

/* Convert the sample pointed by 'src' to 'dst' from hardware format to host */
__api void iio_channel_convert(const struct iio_channel *chn,
		void *dst, const void *src);

/* Convert the sample pointed by 'src' to 'dst' from host format to hardware */
__api void iio_channel_convert_inverse(const struct iio_channel *chn,
		void *dst, const void *src);

/* Functions to read/write the raw stream from the device.
 * The device must be opened first (with iio_device_open). */
__api ssize_t iio_device_read_raw(const struct iio_device *dev,
		void *dst, size_t len, uint32_t *mask, size_t words);
__api ssize_t iio_device_write_raw(const struct iio_device *dev,
		const void *src, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* __IIO_H__ */
