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

#include "iio-backend.h"

#include "iio-config.h"

#include <stdbool.h>

#ifdef _MSC_BUILD
#define inline __inline
#define iio_snprintf sprintf_s
#define iio_sscanf sscanf_s
#else
#define iio_snprintf snprintf
#define iio_sscanf sscanf
#endif

#define ARRAY_SIZE(x) (sizeof(x) ? sizeof(x) / sizeof((x)[0]) : 0)
#define BIT(x) (1 << (x))
#define BIT_MASK(bit) BIT((bit) % 32)
#define BIT_WORD(bit) ((bit) / 32)
#define TEST_BIT(addr, bit) (!!(*(((uint32_t *) addr) + BIT_WORD(bit)) \
		& BIT_MASK(bit)))
#define SET_BIT(addr, bit) \
	*(((uint32_t *) addr) + BIT_WORD(bit)) |= BIT_MASK(bit)
#define CLEAR_BIT(addr, bit) \
	*(((uint32_t *) addr) + BIT_WORD(bit)) &= ~BIT_MASK(bit)

/* https://pubs.opengroup.org/onlinepubs/009695399/basedefs/limits.h.html
 * {NAME_MAX} : Maximum number of bytes in a filename
 * {PATH_MAX} : Maximum number of bytes in a pathname
 * {PAGESIZE} : Size in bytes of a page
 * Too bad we work on non-POSIX systems
 */
#ifndef NAME_MAX
#define NAME_MAX 256
#endif
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#ifndef PAGESIZE
#define PAGESIZE 4096
#endif

#define MAX_CHN_ID     NAME_MAX  /* encoded in the sysfs filename */
#define MAX_CHN_NAME   NAME_MAX  /* encoded in the sysfs filename */
#define MAX_DEV_ID     NAME_MAX  /* encoded in the sysfs filename */
#define MAX_DEV_NAME   NAME_MAX  /* encoded in the sysfs filename */
#define MAX_CTX_NAME   NAME_MAX  /* nominally "xml" */
#define MAX_CTX_DESC   NAME_MAX  /* nominally "linux ..." */
#define MAX_ATTR_NAME  NAME_MAX  /* encoded in the sysfs filename */
#define MAX_ATTR_VALUE (8 * PAGESIZE)  /* 8x Linux page size, could be anything */

/* ntohl/htonl are a nightmare to use in cross-platform applications,
 * since they are defined in different headers on different platforms.
 * iio_be32toh/iio_htobe32 are just clones of ntohl/htonl. */
static inline uint32_t iio_be32toh(uint32_t word)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#ifdef __GNUC__
	return __builtin_bswap32(word);
#else
	return ((word & 0xff) << 24) | ((word & 0xff00) << 8) |
		((word >> 8) & 0xff00) | ((word >> 24) & 0xff);
#endif
#else
	return word;
#endif
}

static inline uint32_t iio_htobe32(uint32_t word)
{
	return iio_be32toh(word);
}

/* Allocate zeroed out memory */
static inline void *zalloc(size_t size)
{
	return calloc(1, size);
}

/*
 * If these structures are updated, the qsort functions defined in sort.c
 * may need to be updated.
 */

struct iio_context_pdata;
struct iio_device_pdata;
struct iio_channel_pdata;
struct iio_scan_backend_context;

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

	char **attrs;
	char **values;
	unsigned int nb_attrs;
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
	enum iio_modifier modifier;
	enum iio_chan_type type;

	struct iio_channel_attr *attrs;
	unsigned int nb_attrs;

	unsigned int number;
};

struct iio_device {
	const struct iio_context *ctx;
	struct iio_device_pdata *pdata;
	void *userdata;

	char *name, *id;

	char **attrs;
	unsigned int nb_attrs;

	char **buffer_attrs;
	unsigned int nb_buffer_attrs;

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

struct iio_context_info {
	char *description;
	char *uri;
};

struct iio_scan_result {
	size_t size;
	struct iio_context_info **info;
};

struct iio_context_info ** iio_scan_result_add(
	struct iio_scan_result *scan_result, size_t num);

void free_channel(struct iio_channel *chn);
void free_device(struct iio_device *dev);

char *iio_channel_get_xml(const struct iio_channel *chn, size_t *len);
char *iio_device_get_xml(const struct iio_device *dev, size_t *len);

char *encode_xml_ndup(const char * input);
char *iio_context_create_xml(const struct iio_context *ctx);
int iio_context_init(struct iio_context *ctx);

bool iio_device_is_tx(const struct iio_device *dev);
int iio_device_open(const struct iio_device *dev,
		size_t samples_count, bool cyclic);
int iio_device_close(const struct iio_device *dev);
int iio_device_set_blocking_mode(const struct iio_device *dev, bool blocking);
ssize_t iio_device_read_raw(const struct iio_device *dev,
		void *dst, size_t len, uint32_t *mask, size_t words);
ssize_t iio_device_write_raw(const struct iio_device *dev,
		const void *src, size_t len);
int iio_device_get_poll_fd(const struct iio_device *dev);

int read_double(const char *str, double *val);
int write_double(char *buf, size_t len, double val);

struct iio_context * local_create_context(void);
struct iio_context * network_create_context(const char *hostname);
struct iio_context * xml_create_context_mem(const char *xml, size_t len);
struct iio_context * xml_create_context(const char *xml_file);
struct iio_context * usb_create_context(unsigned int bus, uint16_t address,
		uint16_t intrfc);
struct iio_context * usb_create_context_from_uri(const char *uri);
struct iio_context * serial_create_context_from_uri(const char *uri);

int local_context_scan(struct iio_scan_result *scan_result);

struct iio_scan_backend_context * usb_context_scan_init(void);
void usb_context_scan_free(struct iio_scan_backend_context *ctx);

int usb_context_scan(struct iio_scan_backend_context *ctx,
		struct iio_scan_result *scan_result);

struct iio_scan_backend_context * dnssd_context_scan_init(void);
void dnssd_context_scan_free(struct iio_scan_backend_context *ctx);

int dnssd_context_scan(struct iio_scan_backend_context *ctx,
		struct iio_scan_result *scan_result);

/* This function is not part of the API, but is used by the IIO daemon */
__api ssize_t iio_device_get_sample_size_mask(const struct iio_device *dev,
		const uint32_t *mask, size_t words);

void iio_channel_init_finalize(struct iio_channel *chn);
unsigned int find_channel_modifier(const char *s, size_t *len_p);

char *iio_strdup(const char *str);
size_t iio_strlcpy(char * __restrict dst, const char * __restrict src, size_t dsize);
char * iio_getenv (char * envvar);

int iio_context_add_device(struct iio_context *ctx, struct iio_device *dev);

int iio_context_add_attr(struct iio_context *ctx,
		const char *key, const char *value);

struct iio_context_pdata * iio_context_get_pdata(const struct iio_context *ctx);

#undef __api

#endif /* __IIO_PRIVATE_H__ */
