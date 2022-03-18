/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2015 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#ifndef _IIOD_CLIENT_H
#define _IIOD_CLIENT_H

#include "iio-backend.h"

#define __api __iio_api

struct iiod_client;
struct iiod_client_io;
struct iiod_client_pdata;

struct iiod_client_ops {
	ssize_t (*write)(struct iiod_client_pdata *desc,
			 const char *src, size_t len);
	ssize_t (*read)(struct iiod_client_pdata *desc,
			char *dst, size_t len);
	ssize_t (*read_line)(struct iiod_client_pdata *desc,
			     char *dst, size_t len);
};

__api void iiod_client_mutex_lock(struct iiod_client *client);
__api void iiod_client_mutex_unlock(struct iiod_client *client);

__api struct iiod_client *
iiod_client_new(const struct iio_context_params *params,
		struct iiod_client_pdata *desc,
		const struct iiod_client_ops *ops);

__api void iiod_client_destroy(struct iiod_client *client);

__api int iiod_client_get_trigger(struct iiod_client *client,
				  const struct iio_device *dev,
				  const struct iio_device **trigger);

__api int iiod_client_set_trigger(struct iiod_client *client,
				  const struct iio_device *dev,
				  const struct iio_device *trigger);

__api int iiod_client_set_kernel_buffers_count(struct iiod_client *client,
					       const struct iio_device *dev,
					       unsigned int nb_blocks);

__api int iiod_client_set_timeout(struct iiod_client *client,
				  unsigned int timeout);

__api ssize_t iiod_client_read_attr(struct iiod_client *client,
				    const struct iio_device *dev,
				    const struct iio_channel *chn,
				    const char *attr, char *dest, size_t len,
				    enum iio_attr_type type,
				    unsigned int buf_id);

__api ssize_t iiod_client_write_attr(struct iiod_client *client,
				     const struct iio_device *dev,
				     const struct iio_channel *chn,
				     const char *attr, const char *src,
				     size_t len, enum iio_attr_type type,
				     unsigned int buf_id);

__api struct iiod_client_io *
iiod_client_open_unlocked(struct iiod_client *client,
			  const struct iio_device *dev,
			  size_t samples_count, bool cyclic);

__api int iiod_client_close_unlocked(struct iiod_client_io *io);

__api ssize_t iiod_client_read(struct iiod_client *client,
			       const struct iio_device *dev,
			       void *dst, size_t len,
			       struct iio_channels_mask *mask);

__api ssize_t iiod_client_write(struct iiod_client *client,
				const struct iio_device *dev,
				const void *src, size_t len);

__api struct iio_context *
iiod_client_create_context(struct iiod_client *client,
			   const struct iio_backend *backend,
			   const char *description,
			   const char **ctx_attrs,
			   const char **ctx_values,
			   unsigned int nb_ctx_attrs);

#undef __api

#endif /* _IIOD_CLIENT_H */
