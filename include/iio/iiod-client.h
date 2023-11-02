/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2015 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#ifndef _IIOD_CLIENT_H
#define _IIOD_CLIENT_H

#include <iio/iio-backend.h>

#define __api __iio_api

struct iiod_client;
struct iiod_client_io;
struct iiod_client_pdata;
struct iio_event_stream_pdata;

struct iiod_client_ops {
	ssize_t (*write)(struct iiod_client_pdata *desc,
			 const char *src, size_t len, unsigned int timeout_ms);
	ssize_t (*read)(struct iiod_client_pdata *desc,
			char *dst, size_t len, unsigned int timeout_ms);
	ssize_t (*read_line)(struct iiod_client_pdata *desc,
			     char *dst, size_t len, unsigned int timeout_ms);
	void (*cancel)(struct iiod_client_pdata *desc);
};

__api void iiod_client_mutex_lock(struct iiod_client *client);
__api void iiod_client_mutex_unlock(struct iiod_client *client);

__api struct iiod_client *
iiod_client_new(const struct iio_context_params *params,
		struct iiod_client_pdata *desc,
		const struct iiod_client_ops *ops);

__api void iiod_client_destroy(struct iiod_client *client);

__api bool iiod_client_uses_binary_interface(const struct iiod_client *client);

__api const struct iio_device *
iiod_client_get_trigger(struct iiod_client *client,
			const struct iio_device *dev);

__api int iiod_client_set_trigger(struct iiod_client *client,
				  const struct iio_device *dev,
				  const struct iio_device *trigger);

__api int iiod_client_set_kernel_buffers_count(struct iiod_client *client,
					       const struct iio_device *dev,
					       unsigned int nb_blocks);

__api int iiod_client_set_timeout(struct iiod_client *client,
				  unsigned int timeout);

__api ssize_t iiod_client_attr_read(struct iiod_client *client,
				    const struct iio_attr *attr,
				    char *dest, size_t len);

__api ssize_t iiod_client_attr_write(struct iiod_client *client,
				     const struct iio_attr *attr,
				     const char *src, size_t len);

__api struct iio_context *
iiod_client_create_context(struct iiod_client *client,
			   const struct iio_backend *backend,
			   const char *description,
			   const char **ctx_attrs,
			   const char **ctx_values,
			   unsigned int nb_ctx_attrs);

__api struct iiod_client_buffer_pdata *
iiod_client_create_buffer(struct iiod_client *client,
			  const struct iio_device *dev, unsigned int idx,
			  struct iio_channels_mask *mask);
__api void iiod_client_free_buffer(struct iiod_client_buffer_pdata *pdata);
__api int iiod_client_enable_buffer(struct iiod_client_buffer_pdata *pdata,
				    size_t nb_samples, bool enable);

__api struct iio_block_pdata *
iiod_client_create_block(struct iiod_client_buffer_pdata *pdata,
			 size_t size, void **data);
__api void iiod_client_free_block(struct iio_block_pdata *block);

__api int iiod_client_enqueue_block(struct iio_block_pdata *block,
				    size_t bytes_used, bool cyclic);

__api int iiod_client_dequeue_block(struct iio_block_pdata *block,
				    bool nonblock);

__api ssize_t iiod_client_readbuf(struct iiod_client_buffer_pdata *pdata,
				  void *dst, size_t len);
__api ssize_t iiod_client_writebuf(struct iiod_client_buffer_pdata *pdata,
				   const void *src, size_t len);

struct iio_event_stream_pdata *
iiod_client_open_event_stream(struct iiod_client *client,
			      const struct iio_device *dev);
void iiod_client_close_event_stream(struct iio_event_stream_pdata *pdata);
int iiod_client_read_event(struct iio_event_stream_pdata *pdata,
			   struct iio_event *out_event,
			   bool nonblock);

#undef __api

#endif /* _IIOD_CLIENT_H */
