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

#include "debug.h"
#include "iio-private.h"
#include "usb-private.h"

#include <errno.h>
#include <libusb-1.0/libusb.h>
#include <stdbool.h>
#include <string.h>

#define DEFAULT_TIMEOUT 1000

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

unsigned int libusb_to_errno(int error)
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

static bool usb_transfer_is_free(const struct libusb_transfer *transfer)
{
	return !transfer->user_data;
}

static bool usb_transfer_set_free(struct libusb_transfer *transfer, bool free)
{
	transfer->user_data = (void *) (uintptr_t) !free;
}

static struct iio_context * usb_clone(const struct iio_context *ctx)
{
	/* TODO(pcercuei): libusb does not allow us to open a USB device twice.
	 * To clone the IIO context, we then need to share the libusb context,
	 * with proper threading protection.
	 *
	 * This trick allows the OSC to work, but it will cause all sorts of
	 * errors when the contexts are destroyed. */
	return ctx;
}

static void usb_transfer_done_cb(struct libusb_transfer *usb_transfer)
{
	int status = usb_transfer->status;

	switch (status) {
	default:
		ERROR("USB transfer error: %i\n", status);
	case LIBUSB_TRANSFER_CANCELLED:
	case LIBUSB_TRANSFER_COMPLETED:
		break;
	}

	usb_transfer_set_free(usb_transfer, true);
}

static struct libusb_transfer * usb_alloc_transfer(
		struct libusb_device_handle *usb_hdl,
		unsigned char endpoint, size_t buf_size,
		libusb_transfer_cb_fn callback, void *d)
{
	unsigned char *buffer;
	struct libusb_transfer *usb_transfer = libusb_alloc_transfer(0);
	if (!usb_transfer)
		return NULL;

	buffer = calloc(1, buf_size);
	if (!buffer) {
		libusb_free_transfer(usb_transfer);
		return NULL;
	}

	libusb_fill_bulk_transfer(usb_transfer, usb_hdl, endpoint,
			buffer, buf_size, callback, d, DEFAULT_TIMEOUT);
	usb_transfer->flags = LIBUSB_TRANSFER_FREE_BUFFER;
	return usb_transfer;
}

static int usb_alloc_transfers(struct iio_device_pdata *pdata, size_t buf_size)
{
	const struct iio_usb_backend *backend = pdata->backend;
	unsigned int i, j;
	unsigned char ep[2] = {
		backend->ep_in | LIBUSB_ENDPOINT_IN,
		backend->ep_out | LIBUSB_ENDPOINT_OUT,
	};

	for (j = 0; j < 2; j++) {
		for (i = 0; i < NB_TRANSFERS; i++) {
			struct libusb_transfer *transfer = usb_alloc_transfer(
					pdata->usb_hdl, ep[j], buf_size,
					usb_transfer_done_cb, NULL);
			if (!transfer)
				goto err_free_in_transfers;

			pdata->transfers[j][i] = transfer;
		}
	}

	return 0;

err_free_out_transfers:
	for (; i; i--)
		libusb_free_transfer(pdata->transfers[1][i - 1]);
	i = NB_TRANSFERS;
err_free_in_transfers:
	for (; i; i--)
		libusb_free_transfer(pdata->transfers[0][i - 1]);
	return -ENOMEM;
}

static void usb_free_transfers(const struct iio_device *dev)
{
	struct libusb_context *usb_ctx = dev->ctx->pdata->usb_ctx;
	struct iio_device_pdata *pdata = dev->pdata;
	unsigned int i, j;

	for (j = 0; j < 2; j++) {
		for (i = 0; i < NB_TRANSFERS; i++) {
			struct libusb_transfer *tr = pdata->transfers[j][i];
			if (!tr)
				continue;

			if (libusb_cancel_transfer(tr))
				continue;

			while (!usb_transfer_is_free(tr))
				libusb_handle_events_completed(usb_ctx, NULL);

			libusb_free_transfer(tr);
			pdata->transfers[j][i] = NULL;
		}
	}
}

static int usb_open(const struct iio_device *dev,
		size_t samples_count, bool cyclic)
{
	struct iio_device_pdata *pdata = dev->pdata;
	const struct iio_usb_backend *backend = pdata->backend;
	struct libusb_transfer *transfer;
	size_t buf_size;
	unsigned int i, j;
	int ret;

	if (pdata->opened)
		return -EBUSY;

	if (pdata->backend->ops && pdata->backend->ops->open) {
		ret = pdata->backend->ops->open(dev, samples_count, cyclic);
		if (ret)
			return ret;
	}

	buf_size = samples_count * iio_device_get_sample_size(dev);
	ret = usb_alloc_transfers(pdata, buf_size);
	if (ret)
		goto err_close_device;

	pdata->next_transfer[0] = 0;
	pdata->next_transfer[1] = 0;

	/* Start the first TX transfer */
	transfer = pdata->transfers[1][0];
	usb_transfer_set_free(transfer, false);
	ret = libusb_submit_transfer(transfer);
	if (ret)
		goto err_set_ret;

	/* Start the RX transfers */
	for (i = 0; i < NB_TRANSFERS; i++) {
		transfer = pdata->transfers[0][i];
		usb_transfer_set_free(transfer, false);
		ret = libusb_submit_transfer(transfer);
		if (ret)
			goto err_set_ret;
	}

	pdata->opened = true;
	return 0;

err_set_ret:
	ret = -libusb_to_errno(ret);
err_free_transfers:
	usb_free_transfers(dev);
err_close_device:
	if (pdata->backend->ops && pdata->backend->ops->close)
		pdata->backend->ops->close(dev);
	return ret;
}

static int usb_close(const struct iio_device *dev)
{
	struct iio_device_pdata *pdata = dev->pdata;
	int ret = 0;

	if (!pdata->opened)
		return -EBADF;

	usb_free_transfers(dev);

	if (pdata->backend->ops && pdata->backend->ops->close)
		ret = pdata->backend->ops->close(dev);

	pdata->opened = false;
	return ret;
}

static ssize_t usb_read_dev_attr(const struct iio_device *dev,
		const char *attr, char *dst, size_t len, bool is_debug)
{
	struct iio_device_pdata *pdata = dev->pdata;

	if (!strcmp(attr, "serial_number")) {
		unsigned char serial_number[32];
		struct libusb_device_descriptor desc;
		int ret;

		ret = libusb_get_device_descriptor(pdata->usb_device, &desc);
		if (ret)
			return (ssize_t) -libusb_to_errno(ret);

		ret = libusb_get_string_descriptor_ascii(pdata->usb_hdl,
				desc.iSerialNumber,
				serial_number, sizeof(serial_number));
		if (ret < 0)
			return (ssize_t) -libusb_to_errno(ret);

		return (ssize_t) snprintf(dst, len, "%s", serial_number);
	}

	if (pdata->backend->ops && pdata->backend->ops->read_device_attr)
		return pdata->backend->ops->read_device_attr(dev,
				attr, dst, len, is_debug);
	else
		return -ENOSYS;
}

static ssize_t usb_read_chn_attr(const struct iio_channel *chn,
		const char *attr, char *dst, size_t len)
{
	struct iio_device_pdata *pdata = chn->dev->pdata;

	if (!strcmp(attr, "index"))
		return (ssize_t) snprintf(dst, len, "%li", chn->index);

	if (!strcmp(attr, "type")) {
		char caps = (chn->format.is_fully_defined ? 'A' - 'a' : 0);
		char sign = chn->format.is_signed ? 's' + caps : 'u' + caps;

		return (ssize_t) snprintf(dst, len, "%ce:%c%u/%u>>%u",
				chn->format.is_be ? 'b' : 'l', sign,
				chn->format.bits, chn->format.length,
				chn->format.shift);
	}

	if (pdata->backend->ops && pdata->backend->ops->read_channel_attr)
		return pdata->backend->ops->read_channel_attr(chn,
				attr, dst, len);
	else
		return -ENOSYS;
}

static ssize_t usb_write_chn_attr(const struct iio_channel *chn,
		const char *attr, const char *src, size_t len)
{
	struct iio_device_pdata *pdata = chn->dev->pdata;

	if (!strcmp(attr, "index") || !strcmp(attr, "type"))
		return -EPERM;

	if (pdata->backend->ops && pdata->backend->ops->write_channel_attr)
		return pdata->backend->ops->write_channel_attr(chn,
				attr, src, len);
	else
		return -ENOSYS;
}

static struct libusb_transfer * usb_get_free_transfer(
		const struct iio_device *dev, bool tx)
{
	struct libusb_context *usb_ctx = dev->ctx->pdata->usb_ctx;
	struct iio_device_pdata *pdata = dev->pdata;
	unsigned int i, next = pdata->next_transfer[tx];
	struct libusb_transfer *transfer = pdata->transfers[tx][next];

	DEBUG("Trying to get %cX buffer\n", tx ? 'T' : 'R');

	while (!usb_transfer_is_free(transfer)) {
		int ret = libusb_handle_events_completed(usb_ctx, NULL);
		if (ret) {
			errno = libusb_to_errno(ret);
			return NULL;
		}
	}

	next++;
	pdata->next_transfer[tx] = (next == NB_TRANSFERS) ? 0 : next;
	usb_transfer_set_free(transfer, false);
	return transfer;
}

static ssize_t usb_get_buffer(const struct iio_device *dev,
		void **addr_ptr, size_t bytes_used,
		uint32_t *mask, size_t words)
{
	struct iio_context_pdata *ctx_pdata = dev->ctx->pdata;
	struct iio_device_pdata *pdata = dev->pdata;
	struct libusb_transfer *transfer;
	void *old_tx, *old_addr;
	bool buf_initialized;
	unsigned int i;

	if (!addr_ptr || words != (dev->nb_channels + 31) / 32)
		return -EINVAL;

	if (!*addr_ptr) {
		/* If addr_ptr contains NULL, we're either initializing a TX
		 * buffer or refilling a RX buffer for the first time.
		 * We need to allocate yet another buffer, so that the
		 * application will always own one when the transfers are
		 * in progress. */

		old_addr = malloc(bytes_used);
		if (!old_addr)
			return -ENOMEM;
	} else {
		old_addr = *addr_ptr;
	}

	/* Get a TX transfer that completed */
	transfer = usb_get_free_transfer(dev, true);
	if (!transfer)
		return -errno;

	/* Give it the new buffer and re-submit it */
	old_tx = transfer->buffer;
	transfer->buffer = old_addr;
	libusb_submit_transfer(transfer);

	/* Get a RX transfer that completed */
	transfer = usb_get_free_transfer(dev, false);
	if (!transfer)
		return -errno;

	/* Return its buffer, and re-submit it with the old TX buffer */
	*addr_ptr = transfer->buffer;

	transfer->buffer = old_tx;
	libusb_submit_transfer(transfer);

	/* Force all channels enabled */
	memset(mask, 0, words * sizeof(*mask));
	for (i = 0; i < dev->nb_channels; i++)
		SET_BIT(mask, dev->channels[i]->index);

	DEBUG("Obtained %zi bytes\n", transfer->actual_length);
	return (ssize_t) transfer->actual_length;
}

static int usb_open_handle(struct iio_device *dev)
{
	struct iio_device_pdata *pdata = dev->pdata;
	int ret;

	if (pdata->usb_hdl)
		return -EBUSY;

	ret = libusb_open(pdata->usb_device, &pdata->usb_hdl);
	if (ret < 0)
		return -libusb_to_errno(ret);

	ret = libusb_claim_interface(pdata->usb_hdl, 0);
	if (ret < 0)
		goto err_libusb_close;

	return 0;

err_libusb_release_interface:
	libusb_release_interface(pdata->usb_hdl, 0);
err_libusb_close:
	libusb_close(pdata->usb_hdl);
	pdata->usb_hdl = NULL;
	return -libusb_to_errno(ret);
}

static void usb_close_handle(struct iio_device_pdata *pdata)
{
	libusb_release_interface(pdata->usb_hdl, 0);
	libusb_close(pdata->usb_hdl);
	pdata->usb_hdl = NULL;
}

static void usb_shutdown(struct iio_context *ctx)
{
	unsigned int i;

	for (i = 0; i < ctx->nb_devices; i++) {
		struct iio_device *dev = ctx->devices[i];
		struct iio_device_pdata *pdata = dev->pdata;

		if (pdata) {
			usb_close(dev);
			usb_close_handle(pdata);

			if (pdata->usb_device)
				libusb_unref_device(pdata->usb_device);
			if (pdata->pdata)
				free(pdata->pdata);
			free(pdata);
		}
	}
}

static const struct iio_backend_ops usb_ops = {
	.clone = usb_clone,
	.open = usb_open,
	.close = usb_close,
	.get_buffer = usb_get_buffer,
	.read_device_attr = usb_read_dev_attr,
	.read_channel_attr = usb_read_chn_attr,
	.write_channel_attr = usb_write_chn_attr,
	.shutdown = usb_shutdown,
};

static bool usb_backend_compatible(struct libusb_device_descriptor *desc,
		const struct iio_usb_backend *backend)
{
	return backend->ids[0] == desc->idVendor
		&& backend->ids[1] == desc->idProduct;
}

static const struct iio_usb_backend * usb_find_backend(
		struct libusb_device_descriptor *desc)
{
#if ENABLE_USB_M1K
	if (usb_backend_compatible(desc, &iio_usb_backend_m1k))
		return &iio_usb_backend_m1k;
#endif

	return NULL;
}

static int usb_add_device(libusb_device *device,
		const struct iio_usb_backend *backend,
		unsigned int id, char **xml, size_t *xml_len)
{
	char buf[128];
	size_t header_len;
	char *ptr;
	size_t len;

	snprintf(buf, sizeof(buf), "<device id=\"iio:device%u\" name=\"%s\">",
			id, backend->name);
	header_len = strlen(buf);

	len = header_len + backend->xml_len + sizeof("</device>");
	ptr = realloc(*xml, *xml_len + len);
	if (!ptr)
		return -ENOMEM;

	*xml = ptr;
	ptr = (char *)((uintptr_t) ptr + *xml_len);
	*xml_len += len - 1;

	snprintf(ptr, len, "%s%s</device>", buf, backend->xml);
	return 0;
}

static const char usb_xml_header[] = "<context name=\"usb\">";
static const char usb_xml_trailer[] = "</context>";

struct iio_context * usb_create_context(void)
{
	unsigned int i, nb_usb_devices, real_nb_devices = 0;
	char *xml, *ptr;
	size_t xml_len;
	int ret;
	struct iio_context *ctx;
	libusb_context *usb_ctx;
	libusb_device **dev_list;
	struct iio_device_pdata *pdata;

	ret = libusb_init(&usb_ctx);
	if (ret)
		return NULL;

	xml_len = xml_header_len;
	xml = malloc(xml_len + sizeof(usb_xml_header));
	if (!xml)
		goto err_libusb_exit;

	strncpy(xml, xml_header, xml_len + 1);
	ptr = (char *)((uintptr_t) xml + xml_len);
	snprintf(ptr, sizeof(usb_xml_header), usb_xml_header);
	xml_len += sizeof(usb_xml_header) - 1;

	libusb_set_debug(usb_ctx, LIBUSB_LOG_LEVEL_WARNING);

	ret = libusb_get_device_list(usb_ctx, &dev_list);
	if (ret < 0)
		goto err_free_xml;

	DEBUG("Detected %i devices\n", ret);

	nb_usb_devices = ret;
	pdata = malloc(nb_usb_devices * sizeof(*pdata));
	if (!pdata)
		goto err_free_device_list;

	for (i = 0; i < nb_usb_devices; i++) {
		struct libusb_device_descriptor desc;
		const struct iio_usb_backend *backend;

		ret = libusb_get_device_descriptor(dev_list[i], &desc);
		if (ret)
			goto err_free_pdata;

		backend = usb_find_backend(&desc);
		if (!backend)
			continue;

		ret = usb_add_device(dev_list[i], backend,
				real_nb_devices, &xml, &xml_len);
		if (ret)
			goto err_free_pdata;

		pdata[real_nb_devices].backend = backend;
		pdata[real_nb_devices].usb_device = dev_list[i];
		real_nb_devices++;
	}

	ptr = realloc(xml, xml_len + sizeof(usb_xml_trailer));
	if (!ptr)
		goto err_free_xml;
	xml = ptr;

	strncpy(xml + xml_len, usb_xml_trailer, sizeof(usb_xml_trailer));
	xml_len += sizeof(usb_xml_trailer) - 1;

	ctx = iio_create_xml_context_mem(xml, xml_len);
	if (!ctx)
		goto err_free_xml;

	ctx->xml = xml;
	ctx->pdata = calloc(1, sizeof(*ctx->pdata));
	if (!ctx->pdata) {
		free(pdata);
		libusb_exit(usb_ctx);
		goto err_destroy_context;
	}

	ctx->pdata->usb_ctx = usb_ctx;
	ctx->name = "usb";
	ctx->description = strdup("USB context");
	ctx->ops = &usb_ops;

	for (i = 0; i < ctx->nb_devices; i++) {
		struct iio_device *dev = ctx->devices[i];

		dev->pdata = calloc(1, sizeof(*dev->pdata));
		if (!dev->pdata) {
			ret = -ENOMEM;
			goto err_destroy_context;
		}

		dev->pdata->backend = pdata[i].backend;
		dev->pdata->usb_device = pdata[i].usb_device;

		ret = usb_open_handle(dev);
		if (ret < 0)
			goto err_destroy_context;

		if (dev->pdata->backend->pdata_size) {
			dev->pdata->pdata = calloc(1,
					dev->pdata->backend->pdata_size);
			if (!dev->pdata->pdata) {
				ret = -ENOMEM;
				goto err_destroy_context;
			}
		}
	}

	free(pdata);
	libusb_free_device_list(dev_list, true);
	return ctx;

err_destroy_context:
	free(pdata);
	libusb_free_device_list(dev_list, true);
	iio_context_destroy(ctx);
	return NULL;

err_free_pdata:
	free(pdata);
err_free_device_list:
	libusb_free_device_list(dev_list, true);
err_free_xml:
	free(xml);
err_libusb_exit:
	libusb_exit(usb_ctx);
	errno = -ret;
	return NULL;
}
