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

#include <ctype.h>
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

#define IIO_INTERFACE_NAME	"IIO"

struct iio_usb_ep_couple {
	unsigned char addr_in, addr_out;
	unsigned int pipe_id;
	bool in_use;

	struct iio_mutex *lock;
};

struct iio_usb_io_context {
	struct iio_usb_ep_couple *ep;

	struct iio_mutex *lock;
	bool cancelled;
	struct libusb_transfer *transfer;
};

struct iio_context_pdata {
	libusb_context *ctx;
	libusb_device_handle *hdl;
	unsigned int interface;

	struct iiod_client *iiod_client;

	/* Lock for non-streaming operations */
	struct iio_mutex *lock;

	/* Lock for endpoint reservation */
	struct iio_mutex *ep_lock;

	struct iio_usb_ep_couple *io_endpoints;
	unsigned int nb_ep_couples;

	unsigned int timeout_ms;

	struct iio_usb_io_context io_ctx;
};

struct iio_device_pdata {
	struct iio_mutex *lock;

	bool opened;
	struct iio_usb_io_context io_ctx;
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

static int usb_io_context_init(struct iio_usb_io_context *io_ctx)
{
	io_ctx->lock = iio_mutex_create();
	if (!io_ctx->lock)
		return -ENOMEM;

	return 0;
}

static void usb_io_context_exit(struct iio_usb_io_context *io_ctx)
{
	if (io_ctx->lock) {
		iio_mutex_destroy(io_ctx->lock);
		io_ctx->lock = NULL;
	}
}

static int usb_get_version(const struct iio_context *ctx,
		unsigned int *major, unsigned int *minor, char git_tag[8])
{
	return iiod_client_get_version(ctx->pdata->iiod_client,
			&ctx->pdata->io_ctx, major, minor, git_tag);
}

static unsigned int usb_calculate_remote_timeout(unsigned int timeout)
{
	/* XXX(pcercuei): We currently hardcode timeout / 2 for the backend used
	 * by the remote. Is there something better to do here? */
	return timeout / 2;
}

#define USB_PIPE_CTRL_TIMEOUT 1000 /* These should not take long */

#define IIO_USD_CMD_RESET_PIPES 0
#define IIO_USD_CMD_OPEN_PIPE 1
#define IIO_USD_CMD_CLOSE_PIPE 2

static int usb_reset_pipes(struct iio_context_pdata *pdata)
{
	int ret;

	ret = libusb_control_transfer(pdata->hdl, LIBUSB_REQUEST_TYPE_VENDOR |
		LIBUSB_RECIPIENT_INTERFACE, IIO_USD_CMD_RESET_PIPES,
		0, pdata->interface, NULL, 0, USB_PIPE_CTRL_TIMEOUT);
	if (ret < 0)
		return -(int) libusb_to_errno(ret);
	return 0;
}

static int usb_open_pipe(struct iio_context_pdata *pdata, unsigned int pipe_id)
{
	int ret;

	ret = libusb_control_transfer(pdata->hdl, LIBUSB_REQUEST_TYPE_VENDOR |
		LIBUSB_RECIPIENT_INTERFACE, IIO_USD_CMD_OPEN_PIPE,
		pipe_id, pdata->interface, NULL, 0, USB_PIPE_CTRL_TIMEOUT);
	if (ret < 0)
		return -(int) libusb_to_errno(ret);
	return 0;
}

static int usb_close_pipe(struct iio_context_pdata *pdata, unsigned int pipe_id)
{
	int ret;

	ret = libusb_control_transfer(pdata->hdl, LIBUSB_REQUEST_TYPE_VENDOR |
		LIBUSB_RECIPIENT_INTERFACE, IIO_USD_CMD_CLOSE_PIPE,
		pipe_id, pdata->interface, NULL, 0, USB_PIPE_CTRL_TIMEOUT);
	if (ret < 0)
		return -(int) libusb_to_errno(ret);
	return 0;
}

static int usb_reserve_ep_unlocked(const struct iio_device *dev)
{
	struct iio_context_pdata *pdata = dev->ctx->pdata;
	unsigned int i;

	for (i = 0; i < pdata->nb_ep_couples; i++) {
		struct iio_usb_ep_couple *ep = &pdata->io_endpoints[i];

		if (!ep->in_use) {
			ep->in_use = true;

			dev->pdata->io_ctx.ep = ep;
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

	for (i = 0; i < pdata->nb_ep_couples; i++) {
		struct iio_usb_ep_couple *ep = &pdata->io_endpoints[i];

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

	pdata->io_ctx.cancelled = false;

	if (pdata->opened)
		goto out_unlock;

	ret = usb_reserve_ep_unlocked(dev);
	if (ret)
		goto out_unlock;

	ret = usb_open_pipe(ctx_pdata, pdata->io_ctx.ep->pipe_id);
	if (ret) {
		char err_str[1024];

		iio_strerror(-ret, err_str, sizeof(err_str));
		ERROR("Failed to open pipe: %s\n", err_str);
		usb_free_ep_unlocked(dev);
		goto out_unlock;
	}

	iio_mutex_lock(pdata->lock);

	ret = iiod_client_open_unlocked(ctx_pdata->iiod_client, &pdata->io_ctx,
			dev, samples_count, cyclic);

	if (!ret) {
		unsigned int remote_timeout =
			usb_calculate_remote_timeout(ctx_pdata->timeout_ms);

		ret = iiod_client_set_timeout(ctx_pdata->iiod_client,
				&pdata->io_ctx, remote_timeout);
	}

	pdata->opened = !ret;

	iio_mutex_unlock(pdata->lock);

	if (ret) {
		usb_close_pipe(ctx_pdata, pdata->io_ctx.ep->pipe_id);
		usb_free_ep_unlocked(dev);
	}

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
	ret = iiod_client_close_unlocked(ctx_pdata->iiod_client, &pdata->io_ctx,
			dev);
	pdata->opened = false;

	iio_mutex_unlock(pdata->lock);

	usb_close_pipe(ctx_pdata, pdata->io_ctx.ep->pipe_id);

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
			&pdata->io_ctx, dev, dst, len, mask, words);
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
			&pdata->io_ctx, dev, src, len);
	iio_mutex_unlock(pdata->lock);

	return ret;
}

static ssize_t usb_read_dev_attr(const struct iio_device *dev,
		const char *attr, char *dst, size_t len, enum iio_attr_type type)
{
	struct iio_context_pdata *pdata = dev->ctx->pdata;

	return iiod_client_read_attr(pdata->iiod_client,
			&pdata->io_ctx, dev, NULL, attr,
			dst, len, type);
}

static ssize_t usb_write_dev_attr(const struct iio_device *dev,
		const char *attr, const char *src, size_t len, enum iio_attr_type type)
{
	struct iio_context_pdata *pdata = dev->ctx->pdata;

	return iiod_client_write_attr(pdata->iiod_client,
			&pdata->io_ctx, dev, NULL, attr,
			src, len, type);
}

static ssize_t usb_read_chn_attr(const struct iio_channel *chn,
		const char *attr, char *dst, size_t len)
{
	struct iio_context_pdata *pdata = chn->dev->ctx->pdata;

	return iiod_client_read_attr(pdata->iiod_client,
			&pdata->io_ctx, chn->dev, chn, attr,
			dst, len, false);
}

static ssize_t usb_write_chn_attr(const struct iio_channel *chn,
		const char *attr, const char *src, size_t len)
{
	struct iio_context_pdata *pdata = chn->dev->ctx->pdata;

	return iiod_client_write_attr(pdata->iiod_client,
			&pdata->io_ctx, chn->dev, chn, attr,
			src, len, false);
}

static int usb_set_kernel_buffers_count(const struct iio_device *dev,
		unsigned int nb_blocks)
{
	struct iio_context_pdata *pdata = dev->ctx->pdata;

	return iiod_client_set_kernel_buffers_count(pdata->iiod_client,
			&pdata->io_ctx, dev, nb_blocks);
}

static int usb_set_timeout(struct iio_context *ctx, unsigned int timeout)
{
	struct iio_context_pdata *pdata = ctx->pdata;
	unsigned int remote_timeout = usb_calculate_remote_timeout(timeout);
	int ret;

	ret = iiod_client_set_timeout(pdata->iiod_client,
			&pdata->io_ctx, remote_timeout);
	if (!ret)
		pdata->timeout_ms = timeout;

	return ret;
}

static void usb_shutdown(struct iio_context *ctx)
{
	unsigned int i;

	usb_io_context_exit(&ctx->pdata->io_ctx);

	for (i = 0; i < ctx->nb_devices; i++)
		usb_close(ctx->devices[i]);

	iio_mutex_destroy(ctx->pdata->lock);
	iio_mutex_destroy(ctx->pdata->ep_lock);

	for (i = 0; i < ctx->pdata->nb_ep_couples; i++)
		if (ctx->pdata->io_endpoints[i].lock)
			iio_mutex_destroy(ctx->pdata->io_endpoints[i].lock);
	if (ctx->pdata->io_endpoints)
		free(ctx->pdata->io_endpoints);

	for (i = 0; i < ctx->nb_devices; i++) {
		struct iio_device *dev = ctx->devices[i];

		usb_io_context_exit(&dev->pdata->io_ctx);
		free(dev->pdata);
	}

	iiod_client_destroy(ctx->pdata->iiod_client);

	usb_reset_pipes(ctx->pdata); /* Close everything */

	libusb_close(ctx->pdata->hdl);
	libusb_exit(ctx->pdata->ctx);
	free(ctx->pdata);
}

static int iio_usb_match_interface(const struct libusb_config_descriptor *desc,
		struct libusb_device_handle *hdl, unsigned int interface)
{
	const struct libusb_interface *iface;
	unsigned int i;

	if (interface >= desc->bNumInterfaces)
		return -EINVAL;

	iface = &desc->interface[interface];

	for (i = 0; i < (unsigned int) iface->num_altsetting; i++) {
		const struct libusb_interface_descriptor *idesc =
			&iface->altsetting[i];
		char name[64];
		int ret;

		if (idesc->iInterface == 0)
			continue;

		ret = libusb_get_string_descriptor_ascii(hdl, idesc->iInterface,
				(unsigned char *) name, sizeof(name));
		if (ret < 0)
			return -(int) libusb_to_errno(ret);

		if (!strcmp(name, IIO_INTERFACE_NAME))
			return (int) i;
	}

	return -EPERM;
}

static int iio_usb_match_device(struct libusb_device *dev,
		struct libusb_device_handle *hdl,
		unsigned int *interface)
{
	struct libusb_config_descriptor *desc;
	unsigned int i;
	int ret;

	ret = libusb_get_active_config_descriptor(dev, &desc);
	if (ret)
		return -(int) libusb_to_errno(ret);

	for (i = 0, ret = -EPERM; ret == -EPERM &&
			i < desc->bNumInterfaces; i++)
		ret = iio_usb_match_interface(desc, hdl, i);

	libusb_free_config_descriptor(desc);
	if (ret < 0)
		return ret;

	DEBUG("Found IIO interface on device %u:%u using interface %u\n",
			libusb_get_bus_number(dev),
			libusb_get_device_address(dev), i - 1);

	*interface = i - 1;
	return ret;
}

static void usb_cancel(const struct iio_device *dev)
{
	struct iio_device_pdata *ppdata = dev->pdata;

	iio_mutex_lock(ppdata->io_ctx.lock);
	if (ppdata->io_ctx.transfer && !ppdata->io_ctx.cancelled)
		libusb_cancel_transfer(ppdata->io_ctx.transfer);
	ppdata->io_ctx.cancelled = true;
	iio_mutex_unlock(ppdata->io_ctx.lock);
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

	.cancel = usb_cancel,
};

static void LIBUSB_CALL sync_transfer_cb(struct libusb_transfer *transfer)
{
	int *completed = transfer->user_data;
	*completed = 1;
}

static int usb_sync_transfer(struct iio_context_pdata *pdata,
	struct iio_usb_io_context *io_ctx, unsigned int ep_type,
	char *data, size_t len, int *transferred)
{
	unsigned int ep;
	struct libusb_transfer *transfer;
	int completed = 0;
	int ret;

	/*
	 * If the size of the data to transfer is too big, the
	 * IOCTL_USBFS_SUBMITURB ioctl (called by libusb) might fail with
	 * errno set to ENOMEM, as the kernel might use contiguous allocation
	 * for the URB if the driver doesn't support scatter-gather.
	 * To prevent that, we support URBs of 1 MiB maximum. The iiod-client
	 * code will handle this properly and ask for a new transfer.
	 */
	if (len > 1 * 1024 * 1024)
		len = 1 * 1024 * 1024;

	if (ep_type == LIBUSB_ENDPOINT_IN)
		ep = io_ctx->ep->addr_in;
	else
		ep = io_ctx->ep->addr_out;

	/*
	 * For cancellation support the check whether the buffer has already been
	 * cancelled and the allocation as well as the assignment of the new
	 * transfer needs to happen in one atomic step. Otherwise it is possible
	 * that the cancellation is missed and transfer is not aborted.
	 */
	iio_mutex_lock(io_ctx->lock);
	if (io_ctx->cancelled) {
		ret = -EBADF;
		goto unlock;
	}

	transfer = libusb_alloc_transfer(0);
	if (!transfer) {
		ret = -ENOMEM;
		goto unlock;
	}

	transfer->user_data = &completed;

	libusb_fill_bulk_transfer(transfer, pdata->hdl, ep,
			(unsigned char *) data, (int) len, sync_transfer_cb,
			&completed, pdata->timeout_ms);
	transfer->type = LIBUSB_TRANSFER_TYPE_BULK;

	ret = libusb_submit_transfer(transfer);
	if (ret) {
		ret = -(int) libusb_to_errno(ret);
		libusb_free_transfer(transfer);
		goto unlock;
	}

	io_ctx->transfer = transfer;
unlock:
	iio_mutex_unlock(io_ctx->lock);
	if (ret)
		return ret;

	while (!completed) {
		ret = libusb_handle_events_completed(pdata->ctx, &completed);
		if (ret < 0) {
			if (ret == LIBUSB_ERROR_INTERRUPTED)
				continue;
			libusb_cancel_transfer(transfer);
			continue;
		}
	}

	switch (transfer->status) {
	case LIBUSB_TRANSFER_COMPLETED:
		*transferred = transfer->actual_length;
		ret = 0;
		break;
	case LIBUSB_TRANSFER_TIMED_OUT:
		ret = -ETIMEDOUT;
		break;
	case LIBUSB_TRANSFER_STALL:
		ret = -EPIPE;
		break;
	case LIBUSB_TRANSFER_NO_DEVICE:
		ret = -ENODEV;
		break;
	case LIBUSB_TRANSFER_CANCELLED:
		ret = -EBADF;
		break;
	default:
		ret = -EIO;
		break;
	}

	/* Same as above. This needs to be atomic in regards to usb_cancel(). */
	iio_mutex_lock(io_ctx->lock);
	io_ctx->transfer = NULL;
	iio_mutex_unlock(io_ctx->lock);

	libusb_free_transfer(transfer);

	return ret;
}

static ssize_t write_data_sync(struct iio_context_pdata *pdata,
		void *ep, const char *data, size_t len)
{
	int transferred, ret;

	ret = usb_sync_transfer(pdata, ep, LIBUSB_ENDPOINT_OUT, (char *) data,
			len, &transferred);
	if (ret)
		return ret;
	else
		return (ssize_t) transferred;
}

static ssize_t read_data_sync(struct iio_context_pdata *pdata,
		void *ep, char *buf, size_t len)
{
	int transferred, ret;

	ret = usb_sync_transfer(pdata, ep, LIBUSB_ENDPOINT_IN, buf, len,
			&transferred);
	if (ret)
		return ret;
	else
		return transferred;
}

static const struct iiod_client_ops usb_iiod_client_ops = {
	.write = write_data_sync,
	.read = read_data_sync,
	.read_line = read_data_sync,
};

static int usb_verify_eps(const struct libusb_interface_descriptor *iface)
{
	unsigned int i, eps = iface->bNumEndpoints;

	/* Check that we have an even number of endpoints, and that input/output
	 * endpoints are interleaved */

	if (eps < 2 || eps % 2)
		return -EINVAL;

	for (i = 0; i < eps; i += 2) {
		if (!(iface->endpoint[i + 0].bEndpointAddress
					& LIBUSB_ENDPOINT_IN))
			return -EINVAL;

		if (iface->endpoint[i + 1].bEndpointAddress
				& LIBUSB_ENDPOINT_IN)
			return -EINVAL;
	}

	return 0;
}

static int usb_populate_context_attrs(struct iio_context *ctx,
		libusb_device *dev, libusb_device_handle *hdl)
{
	struct libusb_device_descriptor dev_desc;
	char buffer[64];
	unsigned int i;
	int ret;

	struct {
		const char *attr;
		uint8_t idx;
	} attrs[3];

	libusb_get_device_descriptor(dev, &dev_desc);

	attrs[0].attr = "usb,vendor";
	attrs[0].idx = dev_desc.iManufacturer;
	attrs[1].attr = "usb,product";
	attrs[1].idx = dev_desc.iProduct;
	attrs[2].attr = "usb,serial";
	attrs[2].idx = dev_desc.iSerialNumber;

	iio_snprintf(buffer, sizeof(buffer), "%04hx", dev_desc.idVendor);
	ret = iio_context_add_attr(ctx, "usb,idVendor", buffer);
	if (ret < 0)
		return ret;

	iio_snprintf(buffer, sizeof(buffer), "%04hx", dev_desc.idProduct);
	ret = iio_context_add_attr(ctx, "usb,idProduct", buffer);
	if (ret < 0)
		return ret;

	iio_snprintf(buffer, sizeof(buffer), "%1hhx.%1hhx",
			(unsigned char)((dev_desc.bcdUSB >> 8) & 0xf),
			(unsigned char)((dev_desc.bcdUSB >> 4) & 0xf));
	ret = iio_context_add_attr(ctx, "usb,release", buffer);
	if (ret < 0)
		return ret;

	for (i = 0; i < ARRAY_SIZE(attrs); i++) {
		if (attrs[i].idx) {
			ret = libusb_get_string_descriptor_ascii(hdl,
					attrs[i].idx, (unsigned char *) buffer,
					sizeof(buffer));
			if (ret < 0)
				return -(int) libusb_to_errno(ret);

			ret = iio_context_add_attr(ctx, attrs[i].attr, buffer);
			if (ret < 0)
				return ret;
		}
	}

#ifdef HAS_LIBUSB_GETVERSION
	/*
	 * libusb_get_version was added 2012-04-17: v1.0.10,
	 * before LIBUSB_API_VERSION was added - Jan 8, 2014
	 * so, you can't use that to determine if it is here
	 */
	{
	struct libusb_version const *ver = libusb_get_version();
	iio_snprintf(buffer, sizeof(buffer), "%i.%i.%i.%i%s",
			ver->major, ver->minor, ver->micro,
			ver->nano, ver->rc);
	ret = iio_context_add_attr(ctx, "usb,libusb", buffer);
	if (ret < 0)
		return ret;
	}
#endif
	return 0;
}

struct iio_context * usb_create_context(unsigned int bus,
		unsigned int address, unsigned int interface)
{
	libusb_context *usb_ctx;
	libusb_device_handle *hdl;
	const struct libusb_interface_descriptor *iface;
	libusb_device *usb_dev;
	struct libusb_config_descriptor *conf_desc;
	libusb_device **device_list;
	struct iio_context *ctx;
	struct iio_context_pdata *pdata;
	char err_str[1024];
	unsigned int i;
	int ret;

	pdata = zalloc(sizeof(*pdata));
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

	libusb_get_device_list(usb_ctx, &device_list);

	usb_dev = NULL;

	for (i = 0; device_list[i]; i++) {
		libusb_device *dev = device_list[i];

		if (bus == libusb_get_bus_number(dev) &&
			address == libusb_get_device_address(dev)) {
			usb_dev = dev;

			ret = libusb_open(usb_dev, &hdl);
			/*
			 * Workaround for libusb on Windows >= 8.1. A device
			 * might appear twice in the list with one device being
			 * bogus and only partially initialized. libusb_open()
			 * returns LIBUSB_ERROR_NOT_SUPPORTED for such devices,
			 * which should never happen for normal devices. So if
			 * we find such a device skip it and keep looking.
			 */
			if (ret == LIBUSB_ERROR_NOT_SUPPORTED) {
				WARNING("Skipping broken USB device. Please upgrade libusb.\n");
				usb_dev = NULL;
				continue;
			}

			break;
		}
	}

	libusb_free_device_list(device_list, true);

	if (!usb_dev) {
		ret = -ENODEV;
		goto err_libusb_exit;
	}

	if (ret) {
		ret = -(int) libusb_to_errno(ret);
		ERROR("Unable to open device\n");
		goto err_libusb_exit;
	}

#if defined(LIBUSB_API_VERSION) && (LIBUSB_API_VERSION >= 0x01000016)
	libusb_set_auto_detach_kernel_driver(hdl, true);
#endif

	ret = libusb_claim_interface(hdl, interface);
	if (ret) {
		ret = -(int) libusb_to_errno(ret);
		ERROR("Unable to claim interface %u:%u:%u: %i\n",
		      bus, address, interface, ret);
		goto err_libusb_close;
	}

	ret = libusb_get_active_config_descriptor(usb_dev, &conf_desc);
	if (ret) {
		ret = -(int) libusb_to_errno(ret);
		ERROR("Unable to get config descriptor: %i\n", ret);
		goto err_libusb_close;
	}

	iface = &conf_desc->interface[interface].altsetting[0];

	ret = usb_verify_eps(iface);
	if (ret) {
		ERROR("Invalid configuration of endpoints\n");
		goto err_free_config_descriptor;
	}

	pdata->nb_ep_couples = iface->bNumEndpoints / 2;

	DEBUG("Found %hhu usable i/o endpoint couples\n", pdata->nb_ep_couples);

	pdata->io_endpoints = calloc(pdata->nb_ep_couples,
			sizeof(*pdata->io_endpoints));
	if (!pdata->io_endpoints) {
		ERROR("Unable to allocate endpoints\n");
		ret = -ENOMEM;
		goto err_free_config_descriptor;
	}

	for (i = 0; i < pdata->nb_ep_couples; i++) {
		struct iio_usb_ep_couple *ep = &pdata->io_endpoints[i];

		ep->addr_in = iface->endpoint[i * 2 + 0].bEndpointAddress;
		ep->addr_out = iface->endpoint[i * 2 + 1].bEndpointAddress;
		ep->pipe_id = i;

		DEBUG("Couple %i with endpoints 0x%x / 0x%x\n", i,
				ep->addr_in, ep->addr_out);

		ep->lock = iio_mutex_create();
		if (!ep->lock) {
			ERROR("Unable to create mutex\n");
			ret = -ENOMEM;
			goto err_free_endpoints;
		}
	}

	pdata->ctx = usb_ctx;
	pdata->hdl = hdl;
	pdata->timeout_ms = DEFAULT_TIMEOUT_MS;
	pdata->interface = interface;

	ret = usb_io_context_init(&pdata->io_ctx);
	if (ret)
		goto err_free_endpoints;

	/* We reserve the first I/O endpoint couple for global operations */
	pdata->io_ctx.ep = &pdata->io_endpoints[0];
	pdata->io_ctx.ep->in_use = true;

	ret = usb_reset_pipes(pdata);
	if (ret) {
		iio_strerror(-ret, err_str, sizeof(err_str));
		ERROR("Failed to reset pipes: %s\n", err_str);
		goto err_io_context_exit;
	}

	ret = usb_open_pipe(pdata, 0);
	if (ret) {
		iio_strerror(-ret, err_str, sizeof(err_str));
		ERROR("Failed to open control pipe: %s\n", err_str);
		goto err_io_context_exit;
	}

	ctx = iiod_client_create_context(pdata->iiod_client, &pdata->io_ctx);
	if (!ctx) {
		ret = -errno;
		goto err_reset_pipes;
	}

	libusb_free_config_descriptor(conf_desc);

	ctx->name = "usb";
	ctx->ops = &usb_ops;
	ctx->pdata = pdata;

	for (i = 0; i < ctx->nb_devices; i++) {
		struct iio_device *dev = ctx->devices[i];

		dev->pdata = zalloc(sizeof(*dev->pdata));
		if (!dev->pdata) {
			ERROR("Unable to allocate memory\n");
			ret = -ENOMEM;
			goto err_context_destroy;
		}

		ret = usb_io_context_init(&dev->pdata->io_ctx);
		if (ret)
			goto err_context_destroy;
	}

	ret = usb_populate_context_attrs(ctx, usb_dev, hdl);
	if (ret < 0)
		goto err_context_destroy;

	return ctx;

err_context_destroy:
	iio_context_destroy(ctx);
	errno = -ret;
	return NULL;

err_reset_pipes:
	usb_reset_pipes(pdata); /* Close everything */
err_io_context_exit:
	usb_io_context_exit(&pdata->io_ctx);
err_free_endpoints:
	for (i = 0; i < pdata->nb_ep_couples; i++)
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

struct iio_context * usb_create_context_from_uri(const char *uri)
{
	long bus, address, interface;
	char *end;
	const char *ptr;

	if (strncmp(uri, "usb:", sizeof("usb:") - 1) != 0)
		goto err_bad_uri;

	ptr = (const char *) ((uintptr_t) uri + sizeof("usb:") - 1);
	if (!isdigit(*ptr))
		goto err_bad_uri;

	bus = strtol(ptr, &end, 10);
	if (ptr == end || *end != '.')
		goto err_bad_uri;

	ptr = (const char *) ((uintptr_t) end + 1);
	if (!isdigit(*ptr))
		goto err_bad_uri;

	address = strtol(ptr, &end, 10);
	if (ptr == end)
		goto err_bad_uri;

	if (*end == '\0') {
		interface = 0;
	} else if (*end == '.') {
		ptr = (const char *) ((uintptr_t) end + 1);
		if (!isdigit(*ptr))
			goto err_bad_uri;

		interface = strtol(ptr, &end, 10);
		if (ptr == end || *end != '\0')
			goto err_bad_uri;
	} else {
		goto err_bad_uri;
	}

	if (bus < 0 || address < 0 || interface < 0)
		goto err_bad_uri;

	return usb_create_context((unsigned int) bus,
			(unsigned int) address, (unsigned int) interface);

err_bad_uri:
	ERROR("Bad URI: \'%s\'\n", uri);
	errno = EINVAL;
	return NULL;
}

static int usb_fill_context_info(struct iio_context_info *info,
		struct libusb_device *dev, struct libusb_device_handle *hdl,
		unsigned int interface)
{
	struct libusb_device_descriptor desc;
	char manufacturer[64], product[64], serial[64];
	char uri[sizeof("usb:127.255.255")];
	char description[sizeof(manufacturer) + sizeof(product) +
		sizeof(serial) + sizeof("0000:0000 ( ), serial=")];
	int ret;

	libusb_get_device_descriptor(dev, &desc);

	iio_snprintf(uri, sizeof(uri), "usb:%d.%d.%u",
		libusb_get_bus_number(dev), libusb_get_device_address(dev),
		interface);

	if (desc.iManufacturer == 0) {
		manufacturer[0] = '\0';
	} else {
		ret = libusb_get_string_descriptor_ascii(hdl,
			desc.iManufacturer,
			(unsigned char *) manufacturer,
			sizeof(manufacturer));
		if (ret < 0)
			manufacturer[0] = '\0';
	}

	if (desc.iProduct == 0) {
		product[0] = '\0';
	} else {
		ret = libusb_get_string_descriptor_ascii(hdl,
			desc.iProduct, (unsigned char *) product,
			sizeof(product));
		if (ret < 0)
			product[0] = '\0';
	}

	if (desc.iSerialNumber == 0) {
		serial[0] = '\0';
	} else {
		ret = libusb_get_string_descriptor_ascii(hdl,
			desc.iSerialNumber, (unsigned char *) serial,
			sizeof(serial));
		if (ret < 0)
			serial[0] = '\0';
	}

	iio_snprintf(description, sizeof(description),
		"%04x:%04x (%s %s), serial=%s", desc.idVendor,
		desc.idProduct, manufacturer, product, serial);

	info->uri = iio_strdup(uri);
	if (!info->uri)
		return -ENOMEM;

	info->description = iio_strdup(description);
	if (!info->description)
		return -ENOMEM;

	return 0;
}

struct iio_scan_backend_context {
	libusb_context *ctx;
};

struct iio_scan_backend_context * usb_context_scan_init(void)
{
	struct iio_scan_backend_context *ctx;
	int ret;

	ctx = malloc(sizeof(*ctx));
	if (!ctx) {
		errno = ENOMEM;
		return NULL;
	}

	ret = libusb_init(&ctx->ctx);
	if (ret) {
		free(ctx);
		errno = (int) libusb_to_errno(ret);
		return NULL;
	}

	return ctx;
}

void usb_context_scan_free(struct iio_scan_backend_context *ctx)
{
	libusb_exit(ctx->ctx);
	free(ctx);
}

int usb_context_scan(struct iio_scan_backend_context *ctx,
		struct iio_scan_result *scan_result)
{
	struct iio_context_info **info;
	libusb_device **device_list;
	unsigned int i;
	int ret;

	ret = libusb_get_device_list(ctx->ctx, &device_list);
	if (ret < 0)
		return -(int) libusb_to_errno(ret);

	for (i = 0; device_list[i]; i++) {
		struct libusb_device_handle *hdl;
		struct libusb_device *dev = device_list[i];
		unsigned int interface = 0;

		ret = libusb_open(dev, &hdl);
		if (ret)
			continue;

		if (!iio_usb_match_device(dev, hdl, &interface)) {
			info = iio_scan_result_add(scan_result, 1);
			if (!info)
				ret = -ENOMEM;
			else
				ret = usb_fill_context_info(*info, dev, hdl,
						interface);
		}

		libusb_close(hdl);
		if (ret < 0)
			goto cleanup_free_device_list;
	}

	ret = 0;

cleanup_free_device_list:
	libusb_free_device_list(device_list, true);
	return ret;
}
