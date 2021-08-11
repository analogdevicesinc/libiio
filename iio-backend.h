/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2020 Analog Devices, Inc.
 */

#ifndef __IIO_BACKEND_H__
#define __IIO_BACKEND_H__

#include <iio.h>

#include <stdbool.h>

#define __api __iio_api

#define __api_export_if(x)	___api_export_if(x)
#define ___api_export_if(x)	___api_export_if_##x
#define ___api_export_if_0
#define ___api_export_if_1	__iio_api_export

/* https://pubs.opengroup.org/onlinepubs/009695399/basedefs/limits.h.html
 * {NAME_MAX} : Maximum number of bytes in a filename
 * {PATH_MAX} : Maximum number of bytes in a pathname
 * {PAGESIZE} : Size in bytes of a page
 * Too bad we work on non-POSIX systems
 */
#ifndef NAME_MAX
#  define NAME_MAX 256
#endif
#ifndef PATH_MAX
#  define PATH_MAX 4096
#endif
#ifndef PAGESIZE
#  define PAGESIZE 4096
#endif

#ifdef _MSC_BUILD
#define inline __inline
#define iio_sscanf sscanf_s
#else
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

static inline __check_ret bool IS_ERR(const void *ptr)
{
	return (uintptr_t) ptr >= (uintptr_t) -4095;
}

static inline __check_ret int PTR_ERR(const void *ptr)
{
	return (int)(intptr_t) ptr;
}

static inline __check_ret void * ERR_PTR(int err)
{
	return (void *)(intptr_t) err;
}

static inline __check_ret int PTR_ERR_OR_ZERO(const void *ptr)
{
	return IS_ERR(ptr) ? PTR_ERR(ptr) : 0;
}

struct iio_device;
struct iio_context;
struct iio_channel;
struct iio_context_pdata;
struct iio_device_pdata;
struct iio_channel_pdata;

enum iio_backend_api_ver {
	IIO_BACKEND_API_V1 = 1,
};

enum iio_attr_type {
	IIO_ATTR_TYPE_DEVICE = 0,
	IIO_ATTR_TYPE_DEBUG,
	IIO_ATTR_TYPE_BUFFER,
};

struct iio_backend_ops {
	int (*scan)(const struct iio_context_params *params,
		    struct iio_scan *ctx);
	struct iio_context * (*create)(const struct iio_context_params *params,
				       const char *uri);
	struct iio_context * (*clone)(const struct iio_context *ctx);
	ssize_t (*read)(const struct iio_device *dev, void *dst, size_t len,
			uint32_t *mask, size_t words);
	ssize_t (*write)(const struct iio_device *dev,
			const void *src, size_t len);
	int (*open)(const struct iio_device *dev,
			size_t samples_count, bool cyclic);
	int (*close)(const struct iio_device *dev);
	int (*get_fd)(const struct iio_device *dev);
	int (*set_blocking_mode)(const struct iio_device *dev, bool blocking);

	void (*cancel)(const struct iio_device *dev);

	int (*set_kernel_buffers_count)(const struct iio_device *dev,
			unsigned int nb_blocks);
	ssize_t (*get_buffer)(const struct iio_device *dev,
			void **addr_ptr, size_t bytes_used,
			uint32_t *mask, size_t words);

	ssize_t (*read_device_attr)(const struct iio_device *dev,
			const char *attr, char *dst, size_t len, enum iio_attr_type);
	ssize_t (*write_device_attr)(const struct iio_device *dev,
			const char *attr, const char *src,
			size_t len, enum iio_attr_type);
	ssize_t (*read_channel_attr)(const struct iio_channel *chn,
			const char *attr, char *dst, size_t len);
	ssize_t (*write_channel_attr)(const struct iio_channel *chn,
			const char *attr, const char *src, size_t len);

	int (*get_trigger)(const struct iio_device *dev,
			const struct iio_device **trigger);
	int (*set_trigger)(const struct iio_device *dev,
			const struct iio_device *trigger);

	void (*shutdown)(struct iio_context *ctx);

	char * (*get_description)(const struct iio_context *ctx);

	int (*get_version)(const struct iio_context *ctx, unsigned int *major,
			unsigned int *minor, char git_tag[8]);

	int (*set_timeout)(struct iio_context *ctx, unsigned int timeout);
};

/**
 * struct iio_backend - IIO backend object (API version 1)
 * @api_version			API version for interfacing with libiio core library
 * @name			Name of this backend
 * @uri_prefix			URI prefix for this backend
 * @ops				Reference to backend ops
 */
struct iio_backend {
	unsigned int			api_version;
	const char			*name;
	const char			*uri_prefix;
	const struct iio_backend_ops	*ops;
	unsigned int			default_timeout_ms;
};

struct iio_context * iio_context_create_from_backend(
		const struct iio_backend *backend,
		const char *description);

__api struct iio_context_pdata *
iio_context_get_pdata(const struct iio_context *ctx);

__api void
iio_context_set_pdata(struct iio_context *ctx, struct iio_context_pdata *data);

__api struct iio_device_pdata *
iio_device_get_pdata(const struct iio_device *dev);

__api void
iio_device_set_pdata(struct iio_device *dev, struct iio_device_pdata *data);

__api struct iio_channel_pdata *
iio_channel_get_pdata(const struct iio_channel *chn);

__api void
iio_channel_set_pdata(struct iio_channel *chn, struct iio_channel_pdata *data);

__api int
iio_scan_add_result(struct iio_scan *ctx, const char *desc, const char *uri);

#if defined(__MINGW32__)
#   define __iio_printf __attribute__((__format__(gnu_printf, 3, 4)))
#elif defined(__GNUC__)
#   define __iio_printf __attribute__((__format__(printf, 3, 4)))
#else
#   define __iio_printf
#endif

__api __iio_printf ssize_t
iio_snprintf(char *buf, size_t len, const char *fmt, ...);
__api char *iio_strdup(const char *str);
__api size_t iio_strlcpy(char * __restrict dst, const char * __restrict src, size_t dsize);

__api struct iio_context *
iio_create_context_from_xml(const struct iio_context_params *params,
			    const char *xml, size_t len,
			    const struct iio_backend *backend,
			    const char *description, const char **ctx_attr,
			    const char **ctx_values, unsigned int nb_ctx_attrs);

/* Allocate zeroed out memory */
static inline void *zalloc(size_t size)
{
	return calloc(1, size);
}

#undef __api

#endif /* __IIO_BACKEND_H__ */
