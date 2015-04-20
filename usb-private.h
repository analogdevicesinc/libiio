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

#ifndef __IIO_USB_PRIVATE_H__
#define __IIO_USB_PRIVATE_H__

#define NB_TRANSFERS 2

#include <stdbool.h>
#include <stdint.h>

struct iio_device;
struct iio_backend_ops;
struct libusb_context;
struct libusb_device_handle;
struct libusb_transfer;

struct iio_usb_device_pdata;

struct iio_usb_backend {
	const uint16_t ids[2];
	const char *xml;
	size_t xml_len;
	const char *name;
	const unsigned int pdata_size;
	const struct iio_backend_ops *ops;
	unsigned char ep_in, ep_out;
};

#if ENABLE_USB_M1K
extern const struct iio_usb_backend iio_usb_backend_m1k;
#endif

struct iio_context_pdata {
	struct libusb_context *usb_ctx;
};

struct iio_device_pdata {
	const struct iio_usb_backend *backend;
	struct iio_usb_device_pdata *pdata;
	struct libusb_device *usb_device;
	struct libusb_device_handle *usb_hdl;
	struct libusb_transfer * transfers[2][NB_TRANSFERS];
	unsigned int next_transfer[2];
	unsigned char serial_number[32];
	bool opened;
};

unsigned int libusb_to_errno(int libusb_error);

#endif /* __IIO_USB_PRIVATE_H__ */
