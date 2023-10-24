/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2020 Analog Devices, Inc.
 */

#ifndef __IIO_BACKEND_H__
#define __IIO_BACKEND_H__

#include <iio/iio.h>

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

struct iio_buffer;
struct iio_device;
struct iio_context;
struct iio_channel;
struct iio_block_pdata;
struct iio_buffer_pdata;
struct iio_context_pdata;
struct iio_device_pdata;
struct iio_channel_pdata;
struct iio_event_stream_pdata;

enum iio_backend_api_ver {
	IIO_BACKEND_API_V1 = 1,
};

enum iio_attr_type {
	IIO_ATTR_TYPE_DEVICE = 0,
	IIO_ATTR_TYPE_DEBUG,
	IIO_ATTR_TYPE_BUFFER,
	IIO_ATTR_TYPE_CHANNEL,
	IIO_ATTR_TYPE_CONTEXT,
};

union iio_pointer {
	const struct iio_context *ctx;
	const struct iio_device *dev;
	const struct iio_channel *chn;
	const struct iio_buffer *buf;
};

struct iio_attr {
	union iio_pointer iio;
	enum iio_attr_type type;
	const char *name;
	const char *filename;
};

struct iio_backend_ops {
	int (*scan)(const struct iio_context_params *params,
		    struct iio_scan *ctx, const char *args);
	struct iio_context * (*create)(const struct iio_context_params *params,
				       const char *uri);

	ssize_t (*read_attr)(const struct iio_attr *attr,
			     char *dst, size_t len);
	ssize_t (*write_attr)(const struct iio_attr *attr,
			      const char *src, size_t len);

	const struct iio_device * (*get_trigger)(const struct iio_device *dev);
	int (*set_trigger)(const struct iio_device *dev,
			const struct iio_device *trigger);

	void (*shutdown)(struct iio_context *ctx);

	int (*get_version)(const struct iio_context *ctx, unsigned int *major,
			unsigned int *minor, char git_tag[8]);

	int (*set_timeout)(struct iio_context *ctx, unsigned int timeout);

	struct iio_buffer_pdata *(*create_buffer)(const struct iio_device *dev,
						  unsigned int idx,
						  struct iio_channels_mask *mask);
	void (*free_buffer)(struct iio_buffer_pdata *pdata);
	int (*enable_buffer)(struct iio_buffer_pdata *pdata,
			     size_t nb_samples, bool enable);
	void (*cancel_buffer)(struct iio_buffer_pdata *pdata);

	ssize_t (*readbuf)(struct iio_buffer_pdata *pdata,
			   void *dst, size_t len);
	ssize_t (*writebuf)(struct iio_buffer_pdata *pdata,
			    const void *src, size_t len);

	struct iio_block_pdata *(*create_block)(struct iio_buffer_pdata *pdata,
						size_t size, void **data);
	void (*free_block)(struct iio_block_pdata *pdata);

	int (*enqueue_block)(struct iio_block_pdata *pdata,
			     size_t bytes_used, bool cyclic);
	int (*dequeue_block)(struct iio_block_pdata *pdata, bool nonblock);

	int (*get_dmabuf_fd)(struct iio_block_pdata *pdata);

	struct iio_event_stream_pdata *(*open_ev)(const struct iio_device *dev);
	void (*close_ev)(struct iio_event_stream_pdata *pdata);
	int (*read_ev)(struct iio_event_stream_pdata *pdata,
		       struct iio_event *out_event,
		       bool nonblock);
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
			    const char *uri, const struct iio_backend *backend,
			    const char *description, const char **ctx_attr,
			    const char **ctx_values, unsigned int nb_ctx_attrs);

/* Allocate zeroed out memory */
static inline void *zalloc(size_t size)
{
	return calloc(1, size);
}

static inline const struct iio_device *
iio_attr_get_device(const struct iio_attr *attr)
{
	switch (attr->type) {
	case IIO_ATTR_TYPE_CONTEXT:
		return NULL;
	case IIO_ATTR_TYPE_CHANNEL:
		return iio_channel_get_device(attr->iio.chn);
	case IIO_ATTR_TYPE_BUFFER:
		return iio_buffer_get_device(attr->iio.buf);
	default:
		return attr->iio.dev;
	}
}

#undef __api

#endif /* __IIO_BACKEND_H__ */
