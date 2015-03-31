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

#ifndef __IIO_PRIVATE_H__
#define __IIO_PRIVATE_H__

/* Include public interface */
#include "iio.h"

#include <stdbool.h>

#ifdef _WIN32
#define snprintf sprintf_s
#define strerror_r(err, buf, len) strerror_s(buf, len, err)
#endif

#define MAX_FACTORY_PROPERTIES 10

#define BIT_MASK(bit) (1 << ((bit) % 32))
#define BIT_WORD(bit) ((bit) / 32)
#define TEST_BIT(addr, bit) (!!(*(((uint32_t *) addr) + BIT_WORD(bit)) \
		& BIT_MASK(bit)))
#define SET_BIT(addr, bit) \
	*(((uint32_t *) addr) + BIT_WORD(bit)) |= BIT_MASK(bit)
#define CLEAR_BIT(addr, bit) \
	*(((uint32_t *) addr) + BIT_WORD(bit)) &= ~BIT_MASK(bit)

enum iio_modifier {
	IIO_NO_MOD,
	IIO_MOD_X,
	IIO_MOD_Y,
	IIO_MOD_Z,
	IIO_MOD_LIGHT_BOTH,
	IIO_MOD_LIGHT_IR,
	IIO_MOD_ROOT_SUM_SQUARED_X_Y,
	IIO_MOD_SUM_SQUARED_X_Y_Z,
	IIO_MOD_LIGHT_CLEAR,
	IIO_MOD_LIGHT_RED,
	IIO_MOD_LIGHT_GREEN,
	IIO_MOD_LIGHT_BLUE,
	IIO_MOD_I,
	IIO_MOD_Q,
};

struct iio_backend_ops {
	struct iio_context * (*clone)(const struct iio_context *ctx);
	ssize_t (*read)(const struct iio_device *dev, void *dst, size_t len,
			uint32_t *mask, size_t words);
	ssize_t (*write)(const struct iio_device *dev,
			const void *src, size_t len);
	int (*open)(const struct iio_device *dev, size_t samples_count,
			uint32_t *mask, size_t words, bool cyclic);
	int (*close)(const struct iio_device *dev);

	ssize_t (*get_buffer)(const struct iio_device *dev,
			void **addr_ptr, size_t bytes_used,
			uint32_t *mask, size_t words);

	ssize_t (*read_device_attr)(const struct iio_device *dev,
			const char *attr, char *dst, size_t len, bool is_debug);
	ssize_t (*write_device_attr)(const struct iio_device *dev,
			const char *attr, const char *src,
			size_t len, bool is_debug);
	ssize_t (*read_channel_attr)(const struct iio_channel *chn,
			const char *attr, char *dst, size_t len);
	ssize_t (*write_channel_attr)(const struct iio_channel *chn,
			const char *attr, const char *src, size_t len);

	int (*get_trigger)(const struct iio_device *dev,
			const struct iio_device **trigger);
	int (*set_trigger)(const struct iio_device *dev,
			const struct iio_device *trigger);

	void (*shutdown)(struct iio_context *ctx);

	int (*get_version)(const struct iio_context *ctx, unsigned int *major,
			unsigned int *minor, char git_tag[8]);

	int (*set_timeout)(struct iio_context *ctx, unsigned int timeout);
};

struct iio_context_pdata;
struct iio_device_pdata;
struct iio_channel_pdata;

struct iio_channel_attr {
	char *name;
	char *filename;
};

struct iio_context {
	struct iio_context_pdata *pdata;
	const struct iio_backend_ops *ops;
	const char *name;
	char *description;

	struct iio_device **devices;
	unsigned int nb_devices;

	char *xml;

	unsigned int rw_timeout_ms;
};

struct iio_channel {
	struct iio_device *dev;
	struct iio_channel_pdata *pdata;
	void *userdata;

	bool is_output;
	bool is_scan_element;
	struct iio_data_format format;
	char *name, *id;
	long index;

	struct iio_channel_attr *attrs;
	unsigned int nb_attrs;
};

struct iio_device {
	const struct iio_context *ctx;
	struct iio_device_pdata *pdata;
	void *userdata;

	char *name, *id;

	char **attrs;
	unsigned int nb_attrs;

	char **debug_attrs;
	unsigned int nb_debug_attrs;

	struct iio_channel **channels;
	unsigned int nb_channels;

	uint32_t *mask;
	size_t words;
};

struct iio_buffer {
	const struct iio_device *dev;
	void *buffer, *userdata;
	size_t length, data_length;

	uint32_t *mask;
	unsigned int dev_sample_size;
	unsigned int sample_size;
	bool is_output, dev_is_high_speed;
};

struct iio_context_factory {
	const char *name;
	struct iio_context * (*create_context)(void);
	struct iio_property properties[MAX_FACTORY_PROPERTIES];
};

void free_channel(struct iio_channel *chn);
void free_device(struct iio_device *dev);

char *iio_channel_get_xml(const struct iio_channel *chn, size_t *len);
char *iio_device_get_xml(const struct iio_device *dev, size_t *len);

char *iio_context_create_xml(const struct iio_context *ctx);
void iio_context_init(struct iio_context *ctx);

bool iio_device_is_tx(const struct iio_device *dev);
int iio_device_open(const struct iio_device *dev,
		size_t samples_count, bool cyclic);
int iio_device_close(const struct iio_device *dev);
ssize_t iio_device_read_raw(const struct iio_device *dev,
		void *dst, size_t len, uint32_t *mask, size_t words);
ssize_t iio_device_write_raw(const struct iio_device *dev,
		const void *src, size_t len);

int read_double(const char *str, double *val);
int write_double(char *buf, size_t len, double val);

struct iio_context * xml_create_context_mem(const char *xml, size_t len);
struct iio_context * xml_create_context(const char *xml_file);

/* This function is not part of the API, but is used by the IIO daemon */
__api ssize_t iio_device_get_sample_size_mask(const struct iio_device *dev,
		uint32_t *mask, size_t words);

int iio_context_factory_register(struct iio_context_factory *factory);
int iio_context_factory_unregister(const char *name);
const char * iio_context_factory_get_property(
		struct iio_context_factory *factory, const char *key);
void iio_context_dump_factories(void);

#endif /* __IIO_PRIVATE_H__ */
