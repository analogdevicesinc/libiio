/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2015 Analog Devices, Inc.
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

#include "iio-lock.h"
#include "iio-private.h"
#include "iiod-client.h"

#include <errno.h>
#include <libusb-1.0/libusb.h>
#include <stdbool.h>
#include <string.h>

#ifdef ERROR
#undef ERROR
#endif

#include "debug.h"

#define DEFAULT_TIMEOUT_MS 5000

/* Endpoint for non-streaming operations */
#define EP_OPS		1

struct iio_usb_io_endpoint {
	unsigned char address;
	bool in_use;

	struct iio_mutex *lock;
};

struct iio_context_pdata {
	libusb_context *ctx;
	libusb_device_handle *hdl;

	struct iiod_client *iiod_client;

	/* Lock for non-streaming operations */
	struct iio_mutex *lock;

	/* Lock for endpoint reservation */
	struct iio_mutex *ep_lock;

	struct iio_usb_io_endpoint *io_endpoints;
	unsigned int nb_io_endpoints;

	unsigned int timeout_ms;
};

struct iio_device_pdata {
	bool is_tx;
	struct iio_mutex *lock;

	bool opened;
	unsigned int ep;
};

static const unsigned int libusb_to_errno_codes[] = {
	[- LIBUSB_ERROR_INVALID_PARAM]	= EINVAL,
	[- LIBUSB_ERROR_ACCESS]		= EACCES,
	[- LIBUSB_ERROR_NO_DEVICE]	= ENODEV,
	[- LIBUSB_ERROR_NOT_FOUND]	= ENXIO,
	[- LIBUSB_ERROR_BUSY]		= EBUSY,
	[- LIBUSB_ERROR_TIMEOUT]	= ETIMEDOUT,
	[- LIBUSB_ERROR_OVERFLOW]	= EIO,
	[- LIBUSB_ERROR_PIPE]		= EPIPE,
	[- LIBUSB_ERROR_INTERRUPTED]	= EINTR,
	[- LIBUSB_ERROR_NO_MEM]		= ENOMEM,
	[- LIBUSB_ERROR_NOT_SUPPORTED]	= ENOSYS,
};

static unsigned int libusb_to_errno(int error)
{
	switch ((enum libusb_error) error) {
	case LIBUSB_ERROR_INVALID_PARAM:
	case LIBUSB_ERROR_ACCESS:
	case LIBUSB_ERROR_NO_DEVICE:
	case LIBUSB_ERROR_NOT_FOUND:
	case LIBUSB_ERROR_BUSY:
	case LIBUSB_ERROR_TIMEOUT:
	case LIBUSB_ERROR_PIPE:
	case LIBUSB_ERROR_INTERRUPTED:
	case LIBUSB_ERROR_NO_MEM:
	case LIBUSB_ERROR_NOT_SUPPORTED:
		return libusb_to_errno_codes[- (int) error];
	case LIBUSB_ERROR_IO:
	case LIBUSB_ERROR_OTHER:
	case LIBUSB_ERROR_OVERFLOW:
	default:
		return EIO;
	}
}

static int usb_get_version(const struct iio_context *ctx,
		unsigned int *major, unsigned int *minor, char git_tag[8])
{
	return iiod_client_get_version(ctx->pdata->iiod_client,
			EP_OPS, major, minor, git_tag);
}

static unsigned int usb_calculate_remote_timeout(unsigned int timeout)
{
	/* XXX(pcercuei): We currently hardcode timeout / 2 for the backend used
	 * by the remote. Is there something better to do here? */
	return timeout / 2;
}

static int usb_reserve_ep_unlocked(const struct iio_device *dev)
{
	struct iio_context_pdata *pdata = dev->ctx->pdata;
	unsigned int i;

	for (i = 0; i < pdata->nb_io_endpoints; i++) {
		struct iio_usb_io_endpoint *ep = &pdata->io_endpoints[i];

		if (!ep->in_use) {
			ep->in_use = true;

			dev->pdata->ep = ep->address;
			dev->pdata->lock = ep->lock;
			return 0;
		}
	}

	return -EBUSY;
}

static void usb_free_ep_unlocked(const struct iio_device *dev)
{
	struct iio_context_pdata *pdata = dev->ctx->pdata;
	unsigned int i;

	for (i = 0; i < pdata->nb_io_endpoints; i++) {
		struct iio_usb_io_endpoint *ep = &pdata->io_endpoints[i];

		if (ep->lock == dev->pdata->lock) {
			ep->in_use = false;
			return;
		}
	}
}

static int usb_open(const struct iio_device *dev,
		size_t samples_count, bool cyclic)
{
	struct iio_context_pdata *ctx_pdata = dev->ctx->pdata;
	struct iio_device_pdata *pdata = dev->pdata;
	int ret = -EBUSY;

	iio_mutex_lock(ctx_pdata->ep_lock);

	if (pdata->opened)
		goto out_unlock;

	ret = usb_reserve_ep_unlocked(dev);
	if (ret)
		goto out_unlock;

	iio_mutex_lock(pdata->lock);

	ret = iiod_client_open_unlocked(ctx_pdata->iiod_client,
			pdata->ep, dev, samples_count, cyclic);

	if (!ret) {
		unsigned int remote_timeout =
			usb_calculate_remote_timeout(ctx_pdata->timeout_ms);

		ret = iiod_client_set_timeout(ctx_pdata->iiod_client,
				pdata->ep, remote_timeout);
	}

	pdata->opened = !ret;

	iio_mutex_unlock(pdata->lock);

	if (ret)
		usb_free_ep_unlocked(dev);

out_unlock:
	iio_mutex_unlock(ctx_pdata->ep_lock);
	return ret;
}

static int usb_close(const struct iio_device *dev)
{
	struct iio_context_pdata *ctx_pdata = dev->ctx->pdata;
	struct iio_device_pdata *pdata = dev->pdata;
	int ret = -EBADF;

	iio_mutex_lock(ctx_pdata->ep_lock);
	if (!pdata->opened)
		goto out_unlock;

	iio_mutex_lock(pdata->lock);
	ret = iiod_client_close_unlocked(ctx_pdata->iiod_client,
			pdata->ep, dev);
	pdata->opened = false;

	usb_free_ep_unlocked(dev);

out_unlock:
	iio_mutex_unlock(ctx_pdata->ep_lock);
	return ret;
}

static ssize_t usb_read(const struct iio_device *dev, void *dst, size_t len,
		uint32_t *mask, size_t words)
{
	struct iio_device_pdata *pdata = dev->pdata;
	ssize_t ret;

	iio_mutex_lock(pdata->lock);
	ret = iiod_client_read_unlocked(dev->ctx->pdata->iiod_client,
			pdata->ep, dev, dst, len, mask, words);
	iio_mutex_unlock(pdata->lock);

	return ret;
}

static ssize_t usb_write(const struct iio_device *dev,
		const void *src, size_t len)
{
	struct iio_device_pdata *pdata = dev->pdata;
	ssize_t ret;

	iio_mutex_lock(pdata->lock);
	ret = iiod_client_write_unlocked(dev->ctx->pdata->iiod_client,
			pdata->ep, dev, src, len);
	iio_mutex_unlock(pdata->lock);

	return ret;
}

static ssize_t usb_read_dev_attr(const struct iio_device *dev,
		const char *attr, char *dst, size_t len, bool is_debug)
{
	struct iio_context_pdata *pdata = dev->ctx->pdata;

	return iiod_client_read_attr(pdata->iiod_client, EP_OPS, dev,
			NULL, attr, dst, len, is_debug);
}

static ssize_t usb_write_dev_attr(const struct iio_device *dev,
		const char *attr, const char *src, size_t len, bool is_debug)
{
	struct iio_context_pdata *pdata = dev->ctx->pdata;

	return iiod_client_write_attr(pdata->iiod_client, EP_OPS, dev,
			NULL, attr, src, len, is_debug);
}

static ssize_t usb_read_chn_attr(const struct iio_channel *chn,
		const char *attr, char *dst, size_t len)
{
	struct iio_context_pdata *pdata = chn->dev->ctx->pdata;

	return iiod_client_read_attr(pdata->iiod_client, EP_OPS, chn->dev,
			chn, attr, dst, len, false);
}

static ssize_t usb_write_chn_attr(const struct iio_channel *chn,
		const char *attr, const char *src, size_t len)
{
	struct iio_context_pdata *pdata = chn->dev->ctx->pdata;

	return iiod_client_write_attr(pdata->iiod_client, EP_OPS, chn->dev,
			chn, attr, src, len, false);
}

static int usb_set_kernel_buffers_count(const struct iio_device *dev,
		unsigned int nb_blocks)
{
	struct iio_context_pdata *pdata = dev->ctx->pdata;

	return iiod_client_set_kernel_buffers_count(pdata->iiod_client,
			EP_OPS, dev, nb_blocks);
}

static int usb_set_timeout(struct iio_context *ctx, unsigned int timeout)
{
	struct iio_context_pdata *pdata = ctx->pdata;
	unsigned int remote_timeout = usb_calculate_remote_timeout(timeout);
	int ret;

	ret = iiod_client_set_timeout(pdata->iiod_client,
			EP_OPS, remote_timeout);
	if (!ret)
		pdata->timeout_ms = timeout;

	return ret;
}

static void usb_shutdown(struct iio_context *ctx)
{
	unsigned int i;

	iio_mutex_destroy(ctx->pdata->lock);
	iio_mutex_destroy(ctx->pdata->ep_lock);

	for (i = 0; i < ctx->pdata->nb_io_endpoints; i++)
		if (ctx->pdata->io_endpoints[i].lock)
			iio_mutex_destroy(ctx->pdata->io_endpoints[i].lock);
	if (ctx->pdata->io_endpoints)
		free(ctx->pdata->io_endpoints);

	iiod_client_destroy(ctx->pdata->iiod_client);

	libusb_close(ctx->pdata->hdl);
	libusb_exit(ctx->pdata->ctx);
}

static const struct iio_backend_ops usb_ops = {
	.get_version = usb_get_version,
	.open = usb_open,
	.close = usb_close,
	.read = usb_read,
	.write = usb_write,
	.read_device_attr = usb_read_dev_attr,
	.read_channel_attr = usb_read_chn_attr,
	.write_device_attr = usb_write_dev_attr,
	.write_channel_attr = usb_write_chn_attr,
	.set_kernel_buffers_count = usb_set_kernel_buffers_count,
	.set_timeout = usb_set_timeout,
	.shutdown = usb_shutdown,
};

static ssize_t write_data_sync(struct iio_context_pdata *pdata,
		int ep, const char *data, size_t len)
{
	int transferred, ret;

	ret = libusb_bulk_transfer(pdata->hdl, ep | LIBUSB_ENDPOINT_OUT,
			(unsigned char *) data, (int) len,
			&transferred, pdata->timeout_ms);
	if (ret)
		return -(int) libusb_to_errno(ret);
	else
		return (size_t) transferred != len ? -EIO : (ssize_t) len;
}

static ssize_t read_data_sync(struct iio_context_pdata *pdata,
		int ep, char *buf, size_t len)
{
	int transferred, ret;

	ret = libusb_bulk_transfer(pdata->hdl, ep | LIBUSB_ENDPOINT_IN,
			(unsigned char *) buf, (int) len, &transferred, pdata->timeout_ms);
	if (ret)
		return -(int) libusb_to_errno(ret);
	else
		return transferred;
}

static const struct iiod_client_ops usb_iiod_client_ops = {
	.write = write_data_sync,
	.read = read_data_sync,
	.read_line = read_data_sync,
};

static int usb_count_io_eps(const struct libusb_interface_descriptor *iface)
{
	unsigned int eps = iface->bNumEndpoints;
	unsigned int i, curr;

	/* Check that for a number of endpoints X provided by the interface, we
	 * have the input and output endpoints in the address range [1, ... X/2]
	 * and that each input endpoint has a corresponding output endpoint at
	 * the same address. */

	if (eps < 2 || eps % 2)
		return -EINVAL;

	for (curr = 1; curr < (eps / 2) + 1; curr++) {
		bool found_in = false, found_out = false;

		for (i = 0; !found_in && i < eps; i++)
			found_in = iface->endpoint[i].bEndpointAddress ==
				(LIBUSB_ENDPOINT_IN | curr);
		if (!found_in)
			return -EINVAL;

		for (i = 0; !found_out && i < eps; i++)
			found_out = iface->endpoint[i].bEndpointAddress ==
				(LIBUSB_ENDPOINT_OUT | curr);
		if (!found_out)
			return -EINVAL;
	}

	/* -1: we reserve the first I/O endpoint couple for global operations */
	return (int) curr - 1;
}

struct iio_context * usb_create_context(unsigned short vid, unsigned short pid)
{
	libusb_context *usb_ctx;
	libusb_device *usb_dev;
	libusb_device_handle *hdl;
	const struct libusb_interface_descriptor *iface;
	struct libusb_config_descriptor *conf_desc;
	struct iio_context *ctx;
	struct iio_context_pdata *pdata;
	unsigned int i;
	int ret;

	pdata = calloc(1, sizeof(*pdata));
	if (!pdata) {
		ERROR("Unable to allocate pdata\n");
		ret = -ENOMEM;
		goto err_set_errno;
	}

	pdata->lock = iio_mutex_create();
	if (!pdata->lock) {
		ERROR("Unable to create mutex\n");
		ret = -ENOMEM;
		goto err_free_pdata;
	}

	pdata->ep_lock = iio_mutex_create();
	if (!pdata->ep_lock) {
		ERROR("Unable to create mutex\n");
		ret = -ENOMEM;
		goto err_destroy_mutex;
	}

	pdata->iiod_client = iiod_client_new(pdata, pdata->lock,
			&usb_iiod_client_ops);
	if (!pdata->iiod_client) {
		ERROR("Unable to create IIOD client\n");
		ret = -errno;
		goto err_destroy_ep_mutex;
	}

	ret = libusb_init(&usb_ctx);
	if (ret) {
		ret = -(int) libusb_to_errno(ret);
		ERROR("Unable to init libusb: %i\n", ret);
		goto err_destroy_iiod_client;
	}

	hdl = libusb_open_device_with_vid_pid(usb_ctx, vid, pid);
	if (!hdl) {
		ERROR("Unable to find device 0x%04hx:0x%04hx\n", vid, pid);
		ret = -ENODEV;
		goto err_libusb_exit;
	}

	libusb_set_auto_detach_kernel_driver(hdl, true);

	ret = libusb_claim_interface(hdl, 0);
	if (ret) {
		ret = -(int) libusb_to_errno(ret);
		ERROR("Unable to claim interface 0: %i\n", ret);
		goto err_libusb_close;
	}

	usb_dev = libusb_get_device(hdl);

	ret = libusb_get_active_config_descriptor(usb_dev, &conf_desc);
	if (ret) {
		ret = -(int) libusb_to_errno(ret);
		ERROR("Unable to get config descriptor: %i\n", ret);
		goto err_libusb_close;
	}

	iface = &conf_desc->interface[0].altsetting[0];

	ret = usb_count_io_eps(iface);
	if (ret < 0) {
		ERROR("Invalid configuration of endpoints\n");
		goto err_free_config_descriptor;
	}

	pdata->nb_io_endpoints = ret;

	DEBUG("Found %hhu usable i/o endpoints\n", pdata->nb_io_endpoints);

	if (pdata->nb_io_endpoints) {
		pdata->io_endpoints = calloc(pdata->nb_io_endpoints,
				sizeof(*pdata->io_endpoints));
		if (!pdata->io_endpoints) {
			ERROR("Unable to allocate endpoints\n");
			ret = -ENOMEM;
			goto err_free_config_descriptor;
		}

		for (i = 0; i < pdata->nb_io_endpoints; i++) {
			struct iio_usb_io_endpoint *ep =
				&pdata->io_endpoints[i];

			/* +2: endpoints start at number 1, and we skip the
			 * endpoint #1 that we reserve for global operations */
			ep->address = i + 2;

			ep->lock = iio_mutex_create();
			if (!ep->lock) {
				ERROR("Unable to create mutex\n");
				ret = -ENOMEM;
				goto err_free_endpoints;
			}
		}
	}

	pdata->ctx = usb_ctx;
	pdata->hdl = hdl;
	pdata->timeout_ms = DEFAULT_TIMEOUT_MS;

	ctx = iiod_client_create_context(pdata->iiod_client, EP_OPS);
	if (!ctx)
		goto err_free_endpoints;

	libusb_free_config_descriptor(conf_desc);

	ctx->name = "usb";
	ctx->ops = &usb_ops;
	ctx->pdata = pdata;

	DEBUG("Initializing context...\n");
	ret = iio_context_init(ctx);
	if (ret < 0)
		goto err_context_destroy;

	for (i = 0; i < ctx->nb_devices; i++) {
		struct iio_device *dev = ctx->devices[i];

		dev->pdata = calloc(1, sizeof(*dev->pdata));
		if (!dev->pdata) {
			ERROR("Unable to allocate memory\n");
			ret = -ENOMEM;
			goto err_context_destroy;
		}

		dev->pdata->is_tx = iio_device_is_tx(dev);
	}

	return ctx;

err_context_destroy:
	iio_context_destroy(ctx);
	errno = -ret;
	return NULL;

err_free_endpoints:
	for (i = 0; i < pdata->nb_io_endpoints; i++)
		if (pdata->io_endpoints[i].lock)
			iio_mutex_destroy(pdata->io_endpoints[i].lock);
	if (pdata->io_endpoints)
		free(pdata->io_endpoints);
err_free_config_descriptor:
	libusb_free_config_descriptor(conf_desc);
err_libusb_close:
	libusb_close(hdl);
err_libusb_exit:
	libusb_exit(usb_ctx);
err_destroy_iiod_client:
	iiod_client_destroy(pdata->iiod_client);
err_destroy_ep_mutex:
	iio_mutex_destroy(pdata->ep_lock);
err_destroy_mutex:
	iio_mutex_destroy(pdata->lock);
err_free_pdata:
	free(pdata);
err_set_errno:
	errno = -ret;
	return NULL;
}
