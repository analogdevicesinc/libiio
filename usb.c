// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2015 - 2020 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "iio-config.h"

#include <iio/iio-backend.h>
#include <iio/iio-debug.h>
#include <iio/iio-lock.h>
#include <iio/iiod-client.h>

#include <ctype.h>
#ifdef __linux__
#include <dirent.h>
#endif
#include <errno.h>
#include <libusb.h>
#include <stdbool.h>
#include <string.h>

/* Endpoint for non-streaming operations */
#define EP_OPS		1

#define IIO_INTERFACE_NAME	"IIO"

struct iio_usb_ep_couple {
	unsigned char addr_in, addr_out;
	uint16_t pipe_id;
	bool in_use;

	const struct iio_device *dev;
};

struct iiod_client_pdata {
	struct iio_usb_ep_couple *ep;
	struct iiod_client *iiod_client;

	struct iio_mutex *lock;
	bool cancelled;
	struct libusb_transfer *transfer;

	struct iio_context_pdata *ctx_pdata;
};

struct iio_context_pdata {
	libusb_context *ctx;
	libusb_device_handle *hdl;
	uint16_t intrfc;

	/* Lock for endpoint reservation */
	struct iio_mutex *ep_lock;

	struct iio_usb_ep_couple *io_endpoints;
	uint16_t nb_ep_couples;

	struct iiod_client_pdata io_ctx;
};

struct iio_buffer_pdata {
	struct iiod_client_pdata io_ctx;
	const struct iio_device *dev;
	struct iiod_client_buffer_pdata *pdata;
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

/* Forward declarations */
static struct iio_context *
usb_create_context_from_args(const struct iio_context_params *params,
			     const char *args);
static int usb_context_scan(const struct iio_context_params *params,
			    struct iio_scan *scan, const char *args);
static ssize_t write_data_sync(struct iiod_client_pdata *ep, const char *data,
			       size_t len, unsigned int timeout_ms);
static ssize_t read_data_sync(struct iiod_client_pdata *ep, char *buf,
			      size_t len, unsigned int timeout_ms);
static void usb_cancel(struct iiod_client_pdata *io_ctx);

static int usb_io_context_init(struct iiod_client_pdata *io_ctx)
{
	io_ctx->lock = iio_mutex_create();

	return iio_err(io_ctx->lock);
}

static void usb_io_context_exit(struct iiod_client_pdata *io_ctx)
{
	if (io_ctx->lock) {
		iio_mutex_destroy(io_ctx->lock);
		io_ctx->lock = NULL;
	}
}

#define USB_PIPE_CTRL_TIMEOUT 1000 /* These should not take long */

#define IIO_USD_CMD_RESET_PIPES 0
#define IIO_USD_CMD_OPEN_PIPE 1
#define IIO_USD_CMD_CLOSE_PIPE 2

static int usb_reset_pipes(struct iio_context_pdata *pdata)
{
	int ret;
/*
	int libusb_control_transfer(libusb_device_handle *devh,
			uint8_t bmRequestType,
			uint8_t bRequest,
			uint16_t wValue,
			uint16_t wIndex,
			unsigned char *data,
			uint16_t wLength,
			unsigned int timeout)
*/
	ret = libusb_control_transfer(pdata->hdl,
			LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_INTERFACE,
			IIO_USD_CMD_RESET_PIPES,
			0,
			pdata->intrfc,
			NULL,
			0,
			USB_PIPE_CTRL_TIMEOUT);
	if (ret < 0)
		return -(int) libusb_to_errno(ret);
	return 0;
}

static int usb_open_pipe(struct iio_context_pdata *pdata, uint16_t pipe_id)
{
	int ret;
/*
	libusb_device_handle *devh,
	uint8_t bmRequestType,
	uint8_t bRequest,
	uint16_t wValue,
	uint16_t wIndex,
	unsigned char *data,
	uint16_t wLength,
	unsigned int timeout)
*/
	ret = libusb_control_transfer(
			pdata->hdl,
			LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_INTERFACE,
			IIO_USD_CMD_OPEN_PIPE,
			pipe_id,
			pdata->intrfc,
			NULL,
			0,
			USB_PIPE_CTRL_TIMEOUT);
	if (ret < 0)
		return -(int) libusb_to_errno(ret);
	return 0;
}

static int usb_close_pipe(struct iio_context_pdata *pdata, uint16_t pipe_id)
{
	int ret;

	ret = libusb_control_transfer(pdata->hdl, LIBUSB_REQUEST_TYPE_VENDOR |
		LIBUSB_RECIPIENT_INTERFACE, IIO_USD_CMD_CLOSE_PIPE,
		pipe_id, pdata->intrfc, NULL, 0, USB_PIPE_CTRL_TIMEOUT);
	if (ret < 0)
		return -(int) libusb_to_errno(ret);
	return 0;
}

static int usb_reserve_ep_unlocked(const struct iio_device *dev,
				   struct iiod_client_pdata *io_ctx)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);
	unsigned int i;

	for (i = 0; i < pdata->nb_ep_couples; i++) {
		struct iio_usb_ep_couple *ep = &pdata->io_endpoints[i];

		if (!ep->in_use) {
			ep->in_use = true;
			ep->dev = dev;
			io_ctx->ep = ep;

			return 0;
		}
	}

	return -EBUSY;
}

static void usb_free_ep_unlocked(const struct iio_device *dev)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);
	unsigned int i;

	for (i = 0; i < pdata->nb_ep_couples; i++) {
		struct iio_usb_ep_couple *ep = &pdata->io_endpoints[i];

		if (ep->dev == dev) {
			ep->in_use = false;
			ep->dev = NULL;
			return;
		}
	}
}

static const struct iiod_client_ops usb_iiod_client_ops = {
	.write = write_data_sync,
	.read = read_data_sync,
	.read_line = read_data_sync,
	.cancel = usb_cancel,
};

static ssize_t
usb_read_attr(const struct iio_attr *attr, char *dst, size_t len)
{
	const struct iio_device *dev = iio_attr_get_device(attr);
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);
	struct iiod_client *client = pdata->io_ctx.iiod_client;

	return iiod_client_attr_read(client, attr, dst, len);
}

static ssize_t
usb_write_attr(const struct iio_attr *attr, const char *src, size_t len)
{
	const struct iio_device *dev = iio_attr_get_device(attr);
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);
	struct iiod_client *client = pdata->io_ctx.iiod_client;

	return iiod_client_attr_write(client, attr, src, len);
}

static int usb_set_timeout(struct iio_context *ctx, unsigned int timeout)
{
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);

	return iiod_client_set_timeout(pdata->io_ctx.iiod_client, timeout);
}

static const struct iio_device * usb_get_trigger(const struct iio_device *dev)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);
	struct iiod_client *client = pdata->io_ctx.iiod_client;

	return iiod_client_get_trigger(client, dev);
}

static int usb_set_trigger(const struct iio_device *dev,
                const struct iio_device *trigger)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);
	struct iiod_client *client = pdata->io_ctx.iiod_client;

	return iiod_client_set_trigger(client, dev, trigger);
}


static void usb_shutdown(struct iio_context *ctx)
{
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);

	iiod_client_destroy(pdata->io_ctx.iiod_client);

	/* TODO: free buffers? */

	usb_io_context_exit(&pdata->io_ctx);
	iio_mutex_destroy(pdata->ep_lock);

	free(pdata->io_endpoints);

	usb_reset_pipes(pdata); /* Close everything */

	libusb_close(pdata->hdl);
	libusb_exit(pdata->ctx);
}

static int iio_usb_match_interface(const struct libusb_config_descriptor *desc,
		struct libusb_device_handle *hdl, unsigned int intrfc)
{
	const struct libusb_interface *iface;
	unsigned int i;

	if (intrfc >= desc->bNumInterfaces)
		return -EINVAL;

	iface = &desc->interface[intrfc];

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
		unsigned int *intrfc)
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

	prm_dbg(NULL, "Found IIO interface on device %u:%u using interface %u\n",
			libusb_get_bus_number(dev),
			libusb_get_device_address(dev), i - 1);

	*intrfc = i - 1;
	return ret;
}

static void usb_cancel(struct iiod_client_pdata *io_ctx)
{
	iio_mutex_lock(io_ctx->lock);

	if (io_ctx->transfer && !io_ctx->cancelled)
		libusb_cancel_transfer(io_ctx->transfer);
	io_ctx->cancelled = true;

	iio_mutex_unlock(io_ctx->lock);
}

static void usb_cancel_buffer(struct iio_buffer_pdata *pdata)
{
	usb_cancel(&pdata->io_ctx);
}

static ssize_t
usb_readbuf(struct iio_buffer_pdata *pdata, void *dst, size_t len)
{
	return iiod_client_readbuf(pdata->pdata, dst, len);
}

static ssize_t
usb_writebuf(struct iio_buffer_pdata *pdata, const void *src, size_t len)
{
	return iiod_client_writebuf(pdata->pdata, src, len);
}

static struct iio_buffer_pdata *
usb_create_buffer(const struct iio_device *dev, unsigned int idx,
		  struct iio_channels_mask *mask)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	const struct iio_context_params *params = iio_context_get_params(ctx);
	struct iio_context_pdata *ctx_pdata = iio_context_get_pdata(ctx);
	struct iio_buffer_pdata *buf;
	int ret;

	buf = zalloc(sizeof(*buf));
	if (!buf)
		return iio_ptr(-ENOMEM);

	ret = usb_io_context_init(&buf->io_ctx);
	if (ret)
		goto err_free_buf;

	iio_mutex_lock(ctx_pdata->ep_lock);

	buf->dev = dev;
	buf->io_ctx.cancelled = false;
	buf->io_ctx.ctx_pdata = ctx_pdata;

	ret = usb_reserve_ep_unlocked(dev, &buf->io_ctx);
	if (ret)
		goto err_unlock;

	ret = usb_open_pipe(ctx_pdata, buf->io_ctx.ep->pipe_id);
	if (ret) {
		dev_perror(dev, ret, "Failed to open pipe");
		goto err_free_ep;
	}

	buf->io_ctx.iiod_client = iiod_client_new(params, &buf->io_ctx,
						  &usb_iiod_client_ops);
	ret = iio_err(buf->io_ctx.iiod_client);
	if (ret) {
		dev_perror(dev, ret, "Failed to created iiod-client");
		goto err_close_pipe;
	}

	buf->pdata = iiod_client_create_buffer(buf->io_ctx.iiod_client,
					       dev, idx, mask);
	ret = iio_err(buf->pdata);
	if (ret) {
		dev_perror(dev, ret, "Unable to create iiod-client buffer");
		goto err_free_iiod_client;
	}

	iio_mutex_unlock(ctx_pdata->ep_lock);

	return buf;

err_free_iiod_client:
	iiod_client_destroy(buf->io_ctx.iiod_client);
err_close_pipe:
	usb_close_pipe(ctx_pdata, buf->io_ctx.ep->pipe_id);
err_free_ep:
	usb_free_ep_unlocked(dev);
err_unlock:
	iio_mutex_unlock(ctx_pdata->ep_lock);
	usb_io_context_exit(&buf->io_ctx);
err_free_buf:
	free(buf);

	return iio_ptr(ret);
}

static void usb_free_buffer(struct iio_buffer_pdata *buf)
{
	const struct iio_context *ctx = iio_device_get_context(buf->dev);
	struct iio_context_pdata *ctx_pdata = iio_context_get_pdata(ctx);

	iiod_client_free_buffer(buf->pdata);

	iio_mutex_lock(ctx_pdata->ep_lock);
	usb_close_pipe(ctx_pdata, buf->io_ctx.ep->pipe_id);
	usb_free_ep_unlocked(buf->dev);
	iio_mutex_unlock(ctx_pdata->ep_lock);

	iiod_client_destroy(buf->io_ctx.iiod_client);

	usb_io_context_exit(&buf->io_ctx);
	free(buf);
}

static int usb_enable_buffer(struct iio_buffer_pdata *pdata,
			     size_t nb_samples, bool enable)
{
	return iiod_client_enable_buffer(pdata->pdata, nb_samples, enable);
}

static struct iio_block_pdata *
usb_create_block(struct iio_buffer_pdata *pdata, size_t size, void **data)
{
	return iiod_client_create_block(pdata->pdata, size, data);
}

static struct iio_event_stream_pdata *
usb_open_events_fd(const struct iio_device *dev)
{
	const struct iio_context *ctx = iio_device_get_context(dev);
	struct iio_context_pdata *pdata = iio_context_get_pdata(ctx);

	return iiod_client_open_event_stream(pdata->io_ctx.iiod_client, dev);
}

static const struct iio_backend_ops usb_ops = {
	.scan = usb_context_scan,
	.create = usb_create_context_from_args,
	.read_attr = usb_read_attr,
	.write_attr = usb_write_attr,
	.get_trigger = usb_get_trigger,
	.set_trigger = usb_set_trigger,
	.set_timeout = usb_set_timeout,
	.shutdown = usb_shutdown,

	.create_buffer = usb_create_buffer,
	.free_buffer = usb_free_buffer,
	.enable_buffer = usb_enable_buffer,
	.cancel_buffer = usb_cancel_buffer,

	.readbuf = usb_readbuf,
	.writebuf = usb_writebuf,

	.create_block = usb_create_block,
	.free_block = iiod_client_free_block,
	.enqueue_block = iiod_client_enqueue_block,
	.dequeue_block = iiod_client_dequeue_block,

	.open_ev = usb_open_events_fd,
	.close_ev = iiod_client_close_event_stream,
	.read_ev = iiod_client_read_event,
};

__api_export_if(WITH_USB_BACKEND_DYNAMIC)
const struct iio_backend iio_usb_backend = {
	.api_version = IIO_BACKEND_API_V1,
	.name = "usb",
	.uri_prefix = "usb:",
	.ops = &usb_ops,
	.default_timeout_ms = 5000,
};

static void LIBUSB_CALL sync_transfer_cb(struct libusb_transfer *transfer)
{
	int *completed = transfer->user_data;
	*completed = 1;
}

static int usb_sync_transfer(struct iio_context_pdata *pdata,
			     struct iiod_client_pdata *io_ctx,
			     unsigned int ep_type, char *data, size_t len,
			     int *transferred, unsigned int timeout_ms)
{
	unsigned char ep;
	struct libusb_transfer *transfer = NULL;
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
			&completed, timeout_ms);
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

static ssize_t write_data_sync(struct iiod_client_pdata *ep,
			       const char *data, size_t len,
			       unsigned int timeout_ms)
{
	int transferred, ret;

	ret = usb_sync_transfer(ep->ctx_pdata, ep, LIBUSB_ENDPOINT_OUT,
				(char *) data, len, &transferred, timeout_ms);
	if (ret)
		return ret;
	else
		return (ssize_t) transferred;
}

static ssize_t read_data_sync(struct iiod_client_pdata *ep,
			      char *buf, size_t len, unsigned int timeout_ms)
{
	int transferred, ret;

	ret = usb_sync_transfer(ep->ctx_pdata, ep, LIBUSB_ENDPOINT_IN,
				buf, len, &transferred, timeout_ms);
	if (ret)
		return ret;
	else
		return transferred;
}

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

static int usb_get_string(libusb_device_handle *hdl, uint8_t idx,
			  char *buffer, size_t length)
{
	int ret;

	ret = libusb_get_string_descriptor_ascii(hdl, idx,
						 (unsigned char *) buffer,
						 (int) length);
	if (ret < 0) {
		buffer[0] = '\0';
		return -(int) libusb_to_errno(ret);
	}

	return 0;
}

static int usb_get_description(struct libusb_device_handle *hdl,
			       const struct libusb_device_descriptor *desc,
			       char *buffer, size_t length)
{
	char manufacturer[64], product[64], serial[64];
	ssize_t ret;

	manufacturer[0] = '\0';
	if (desc->iManufacturer > 0) {
		usb_get_string(hdl, desc->iManufacturer,
			       manufacturer, sizeof(manufacturer));
	}

	product[0] = '\0';
	if (desc->iProduct > 0) {
		usb_get_string(hdl, desc->iProduct,
			       product, sizeof(product));
	}

	serial[0] = '\0';
	if (desc->iSerialNumber > 0) {
		usb_get_string(hdl, desc->iSerialNumber,
			       serial, sizeof(serial));
	}

	ret = iio_snprintf(buffer, length,
			   "%04x:%04x (%s %s), serial=%s", desc->idVendor,
			   desc->idProduct, manufacturer, product, serial);
	if (ret < 0)
		return (int) ret;

	return 0;
}

static struct iio_context *
usb_create_context_with_attrs(libusb_device *usb_dev,
			      struct iio_context_pdata *pdata)
{
	struct libusb_version const *libusb_version = libusb_get_version();
	struct libusb_device_descriptor dev_desc;
	char vendor[64], product[64], serial[64],
	     uri[sizeof("usb:127.255.255")],
	     idVendor[5], idProduct[5], version[4],
	     lib_version[16], description[256];
	const char *attr_names[] = {
		"uri",
		"usb,vendor",
		"usb,product",
		"usb,serial",
		"usb,idVendor",
		"usb,idProduct",
		"usb,release",
		"usb,libusb",
	};
	char *attr_values[ARRAY_SIZE(attr_names)] = {
		uri,
		vendor,
		product,
		serial,
		idVendor,
		idProduct,
		version,
		lib_version,
	};
	struct iiod_client *client = pdata->io_ctx.iiod_client;

	libusb_get_device_descriptor(usb_dev, &dev_desc);

	usb_get_description(pdata->hdl, &dev_desc,
			    description, sizeof(description));

	iio_snprintf(uri, sizeof(uri), "usb:%d.%d.%u",
		     libusb_get_bus_number(usb_dev),
		     libusb_get_device_address(usb_dev),
		     (uint8_t)pdata->intrfc);
	usb_get_string(pdata->hdl, dev_desc.iManufacturer,
		       vendor, sizeof(vendor));
	usb_get_string(pdata->hdl, dev_desc.iProduct,
		       product, sizeof(product));
	usb_get_string(pdata->hdl, dev_desc.iSerialNumber,
		       serial, sizeof(serial));
	iio_snprintf(idVendor, sizeof(idVendor), "%04hx", dev_desc.idVendor);
	iio_snprintf(idProduct, sizeof(idProduct), "%04hx", dev_desc.idProduct);
	iio_snprintf(version, sizeof(version), "%1hhx.%1hhx",
		     (unsigned char)((dev_desc.bcdUSB >> 8) & 0xf),
		     (unsigned char)((dev_desc.bcdUSB >> 4) & 0xf));
	iio_snprintf(lib_version, sizeof(lib_version), "%i.%i.%i.%i%s",
		     libusb_version->major, libusb_version->minor,
		     libusb_version->micro, libusb_version->nano,
		     libusb_version->rc);

	return iiod_client_create_context(client,
					  &iio_usb_backend, description,
					  attr_names,
					  (const char **) attr_values,
					  ARRAY_SIZE(attr_names));
}

static struct iio_context * usb_create_context(const struct iio_context_params *params,
					       unsigned int bus,
					       uint16_t address, uint16_t intrfc)
{
	libusb_context *usb_ctx;
	libusb_device_handle *hdl = NULL;
	const struct libusb_interface_descriptor *iface;
	libusb_device *usb_dev;
	struct libusb_config_descriptor *conf_desc;
	libusb_device **device_list;
	struct iio_context *ctx;
	struct iio_context_pdata *pdata;
	uint16_t i;
	int ret;

	pdata = zalloc(sizeof(*pdata));
	if (!pdata) {
		prm_err(params, "Unable to allocate pdata\n");
		ret = -ENOMEM;
		goto err_set_errno;
	}

	pdata->ep_lock = iio_mutex_create();
	ret = iio_err(pdata->ep_lock);
	if (ret) {
		prm_err(params, "Unable to create mutex\n");
		goto err_free_pdata;
	}

	ret = libusb_init(&usb_ctx);
	if (ret) {
		ret = -(int) libusb_to_errno(ret);
		prm_perror(params, ret, "Unable to init libusb");
		goto err_destroy_ep_mutex;
	}

	ret = (int) libusb_get_device_list(usb_ctx, &device_list);
	if (ret < 0) {
		ret = -(int) libusb_to_errno(ret);
		prm_perror(params, ret, "Unable to get usb device list");
		goto err_libusb_exit;
	}

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
				prm_warn(params, "Skipping broken USB device. Please upgrade libusb.\n");
				usb_dev = NULL;
				continue;
			}

			break;
		}
	}

	libusb_free_device_list(device_list, true);

	if (!usb_dev || !hdl) {
		ret = -ENODEV;
		goto err_libusb_exit;
	}

	if (ret) {
		ret = -(int) libusb_to_errno(ret);
		prm_perror(params, ret, "Unable to open device\n");
		goto err_libusb_exit;
	}

#if defined(LIBUSB_API_VERSION) && (LIBUSB_API_VERSION >= 0x01000016)
	libusb_set_auto_detach_kernel_driver(hdl, true);
#endif

	ret = libusb_claim_interface(hdl, intrfc);
	if (ret) {
		ret = -(int) libusb_to_errno(ret);
		prm_perror(params, ret, "Unable to claim interface %u:%u:%u",
			   bus, address, intrfc);
		goto err_libusb_close;
	}

	ret = libusb_get_active_config_descriptor(usb_dev, &conf_desc);
	if (ret) {
		ret = -(int) libusb_to_errno(ret);
		prm_perror(params, ret, "Unable to get config descriptor");
		goto err_libusb_close;
	}

	iface = &conf_desc->interface[intrfc].altsetting[0];

	ret = usb_verify_eps(iface);
	if (ret) {
		prm_perror(params, ret, "Invalid configuration of endpoints");
		goto err_free_config_descriptor;
	}

	pdata->nb_ep_couples = iface->bNumEndpoints / 2;

	prm_dbg(params, "Found %hu usable i/o endpoint couples\n",
		pdata->nb_ep_couples);

	pdata->io_endpoints = calloc(pdata->nb_ep_couples,
			sizeof(*pdata->io_endpoints));
	if (!pdata->io_endpoints) {
		prm_err(params, "Unable to allocate endpoints\n");
		ret = -ENOMEM;
		goto err_free_config_descriptor;
	}

	for (i = 0; i < pdata->nb_ep_couples; i++) {
		struct iio_usb_ep_couple *ep = &pdata->io_endpoints[i];

		ep->addr_in = iface->endpoint[i * 2 + 0].bEndpointAddress;
		ep->addr_out = iface->endpoint[i * 2 + 1].bEndpointAddress;
		ep->pipe_id = i;

		prm_dbg(params, "Couple %i with endpoints 0x%x / 0x%x\n", i,
			ep->addr_in, ep->addr_out);
	}

	pdata->ctx = usb_ctx;
	pdata->hdl = hdl;
	pdata->intrfc = intrfc;

	ret = usb_io_context_init(&pdata->io_ctx);
	if (ret)
		goto err_free_endpoints;

	/* We reserve the first I/O endpoint couple for global operations */
	pdata->io_ctx.ep = &pdata->io_endpoints[0];
	pdata->io_ctx.ep->in_use = true;

	pdata->io_ctx.ctx_pdata = pdata;

	ret = usb_reset_pipes(pdata);
	if (ret) {
		prm_perror(params, ret, "Failed to reset pipes");
		goto err_io_context_exit;
	}

	ret = usb_open_pipe(pdata, 0);
	if (ret) {
		prm_perror(params, ret, "Failed to open control pipe");
		goto err_io_context_exit;
	}

	pdata->io_ctx.iiod_client = iiod_client_new(params, &pdata->io_ctx,
						    &usb_iiod_client_ops);
	ret = iio_err(pdata->io_ctx.iiod_client);
	if (ret) {
		prm_perror(params, ret, "Unable to create IIOD client");
		goto err_reset_pipes;
	}

	ctx = usb_create_context_with_attrs(usb_dev, pdata);
	ret = iio_err(ctx);
	if (ret)
		goto err_free_iiod_client;

	libusb_free_config_descriptor(conf_desc);

	iio_context_set_pdata(ctx, pdata);

	return ctx;

err_reset_pipes:
	usb_reset_pipes(pdata); /* Close everything */
err_free_iiod_client:
	iiod_client_destroy(pdata->io_ctx.iiod_client);
err_io_context_exit:
	usb_io_context_exit(&pdata->io_ctx);
err_free_endpoints:
	free(pdata->io_endpoints);
err_free_config_descriptor:
	libusb_free_config_descriptor(conf_desc);
err_libusb_close:
	libusb_close(hdl);
err_libusb_exit:
	libusb_exit(usb_ctx);
err_destroy_ep_mutex:
	iio_mutex_destroy(pdata->ep_lock);
err_free_pdata:
	free(pdata);
err_set_errno:
	return iio_ptr(ret);
}

static struct iio_context *
usb_create_context_from_args(const struct iio_context_params *params,
			     const char *args)
{
	long bus, address, intrfc;
	char *end;
	const char *ptr = args;
	/* keep MSVS happy by setting these to NULL */
	struct iio_scan *scan_ctx = NULL;
	bool scan;
	int err = -EINVAL;

	/* if uri is just "usb:" that means search for the first one */
	scan = !*ptr;
	if (scan) {
		scan_ctx = iio_scan(params, "usb");
		if (iio_err(scan_ctx)) {
			err = iio_err(scan_ctx);
			goto err_bad_uri;
		}

		if (iio_scan_get_results_count(scan_ctx) != 1) {
			err = -ENXIO;
			goto err_bad_uri;
		}

		ptr = iio_scan_get_uri(scan_ctx, 0);
		ptr += sizeof("usb:") - 1;
	}

	if (!isdigit(*ptr))
		goto err_bad_uri;

	errno = 0;
	bus = strtol(ptr, &end, 10);
	if (ptr == end || *end != '.' || errno == ERANGE || bus < 0 || bus > UINT8_MAX)
		goto err_bad_uri;

	ptr = (const char *) ((uintptr_t) end + 1);
	if (!isdigit(*ptr))
		goto err_bad_uri;

	errno = 0;
	address = strtol(ptr, &end, 10);
	if (ptr == end || errno == ERANGE || address < 0 || address > UINT8_MAX)
		goto err_bad_uri;

	if (*end == '\0') {
		intrfc = 0;
	} else if (*end == '.') {
		ptr = (const char *) ((uintptr_t) end + 1);
		if (!isdigit(*ptr))
			goto err_bad_uri;

		errno = 0;
		intrfc = strtol(ptr, &end, 10);
		if (ptr == end || *end != '\0' || errno == ERANGE || intrfc < 0 || intrfc > UINT8_MAX)
			goto err_bad_uri;
	} else {
		goto err_bad_uri;
	}

	if (scan)
		iio_scan_destroy(scan_ctx);

	return usb_create_context(params, (unsigned int) bus,
			(uint16_t) address, (uint16_t) intrfc);

err_bad_uri:
	if (scan)
		iio_scan_destroy(scan_ctx);

	prm_err(params, "Bad URI: \'usb:%s\'\n", args);
	return iio_ptr(err);
}

static int usb_add_context_info(struct iio_scan *scan,
				struct libusb_device *dev,
				struct libusb_device_handle *hdl,
				unsigned int intrfc)
{
	struct libusb_device_descriptor desc;
	char uri[sizeof("usb:127.255.255")];
	char description[256];
	int ret;

	libusb_get_device_descriptor(dev, &desc);

	ret = usb_get_description(hdl, &desc, description, sizeof(description));
	if (ret)
		return ret;

	iio_snprintf(uri, sizeof(uri), "usb:%d.%d.%u",
		libusb_get_bus_number(dev), libusb_get_device_address(dev),
		intrfc);

	return iio_scan_add_result(scan, description, uri);
}

static int parse_vid_pid(const char *vid_pid, uint16_t *vid, uint16_t *pid)
{
	unsigned long val;
	char *ptr;

	/*
	 * vid_pid string must be either:
	 * - NULL: scan everything,
	 * - "vid:*": scan all devices with the given VID,
	 * - "vid:pid": scan the device with the given VID/PID.
	 * IDs are given in hexadecimal, and the 0x prefix is not required.
	 */

	*vid = 0;
	*pid = 0;

	if (!vid_pid)
		return 0;

	errno = 0;
	val = strtoul(vid_pid, &ptr, 16);
	if (ptr == vid_pid || val > 0xFFFF || *ptr != ':' || errno == ERANGE)
		return -EINVAL;

	*vid = (uint16_t) val;

	vid_pid = ptr + 1;

	if (*vid_pid == '*')
		return vid_pid[1] == '\0' ? 0 : -EINVAL;

	errno = 0;
	val = strtoul(vid_pid, &ptr, 16);
	if (ptr == vid_pid || val > 0xFFFF || *ptr != '\0' || errno == ERANGE)
		return -EINVAL;

	*pid = (uint16_t) val;

	return 0;
}

static int usb_context_scan(const struct iio_context_params *params,
			    struct iio_scan *scan, const char *args)
{
	libusb_device **device_list;
	libusb_context *ctx;
	uint16_t vid, pid;
	unsigned int i;
	int ret;

	ret = parse_vid_pid(args, &vid, &pid);
	if (ret)
		return ret;

	ret = libusb_init(&ctx);
	if (ret < 0) {
#ifdef __linux__
		/* When Linux's OTG USB is in device mode, and there are no hosts,
		 * libusb_init() is expected to fail, but we shouldn't treat that
		 * as a hard failure - only that there are no devices.
		 */
		DIR* dir = opendir("/dev/bus/usb/");
		if (dir)
			closedir(dir);
		else if (errno == ENOENT)
			return 0;
#endif
		return -(int) libusb_to_errno(ret);
	}

	ret = (int) libusb_get_device_list(ctx, &device_list);
	if (ret < 0) {
		ret = -(int) libusb_to_errno(ret);
		goto cleanup_libusb_exit;
	}

	for (i = 0; device_list[i]; i++) {
		struct libusb_device_handle *hdl;
		struct libusb_device *dev = device_list[i];
		unsigned int intrfc = 0;
		struct libusb_device_descriptor device_descriptor;

		/* If we are given a pid or vid, use that to qualify for things,
		 * this avoids open/closing random devices & potentially locking
		 * (blocking them) from other applications
		 */
		if(vid || pid) {
			ret = libusb_get_device_descriptor(dev, &device_descriptor);
			if (ret)
				continue;
			if (vid && vid != device_descriptor.idVendor)
				continue;
			if (pid && pid != device_descriptor.idProduct)
				continue;
		}

		ret = libusb_open(dev, &hdl);
		if (ret)
			continue;

		if (!iio_usb_match_device(dev, hdl, &intrfc))
			ret = usb_add_context_info(scan, dev, hdl, intrfc);

		libusb_close(hdl);
		if (ret < 0)
			goto cleanup_free_device_list;
	}

	ret = 0;

cleanup_free_device_list:
	libusb_free_device_list(device_list, true);
cleanup_libusb_exit:
	libusb_exit(ctx);
	return ret;
}
