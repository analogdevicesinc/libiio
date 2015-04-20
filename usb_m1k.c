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
#include <math.h>
#include <string.h>

#define M1K_CMD_GET_INFO	0x00
#define M1K_CMD_SET_MODE	0x53
#define M1K_CMD_SET_PSET	0x59
#define M1K_CMD_GET_FRAME_NB	0x6F
#define M1K_CMD_RESET		0xC5
#define M1K_CMD_HW_CONFIG	0xCC
#define M1K_CMD_SET_DATA_FMT	0xDD

enum usb_m1k_channel_mode {
	USB_M1K_MODE_DISABLED,
	USB_M1K_MODE_SVMI,
	USB_M1K_MODE_SIMV,
	_USB_M1K_MODE_LAST,
};

struct iio_usb_device_pdata {
	enum usb_m1k_channel_mode modes[2];
	bool opened;
};

static const char * mode_names[_USB_M1K_MODE_LAST] = {
	[USB_M1K_MODE_DISABLED]	= "D",
	[USB_M1K_MODE_SVMI]	= "V",
	[USB_M1K_MODE_SIMV]	= "I",
};

static const unsigned int mode_to_pset[_USB_M1K_MODE_LAST] = {
	[USB_M1K_MODE_DISABLED]	= 0x3000,
	[USB_M1K_MODE_SVMI]	= 0x0000,
	[USB_M1K_MODE_SIMV]	= 0x7f7f,
};

static ssize_t usb_m1k_read_chn_attr(const struct iio_channel *chn,
		const char *attr, char *dst, size_t len)
{
	struct iio_usb_device_pdata *pdata = chn->dev->pdata->pdata;

	if (!strcmp(attr, "mode"))
		return (ssize_t) snprintf(dst, len, "%s",
				mode_names[pdata->modes[chn->index]]);

	return -ENOENT;
}

static ssize_t usb_m1k_set_mode(struct iio_device_pdata *pdata,
		enum usb_m1k_channel_mode mode, unsigned int index)
{
	uint16_t flags = LIBUSB_REQUEST_TYPE_VENDOR |
		LIBUSB_RECIPIENT_DEVICE | LIBUSB_ENDPOINT_OUT;
	uint16_t val = (uint16_t) mode_to_pset[mode];
	int ret = libusb_control_transfer(pdata->usb_hdl, flags,
			M1K_CMD_SET_PSET, index, val, NULL, 0, 0);
	if (ret < 0)
		return -libusb_to_errno(ret);

	val = (uint16_t) mode;
	ret = libusb_control_transfer(pdata->usb_hdl, flags,
			M1K_CMD_SET_MODE, index, val, NULL, 0, 0);
	if (ret < 0)
		return -libusb_to_errno(ret);

	DEBUG("Setting mode %s for channel %u\n", mode_names[mode], index);
	pdata->pdata->modes[index] = mode;
	return 0;
}

static ssize_t usb_m1k_write_chn_attr(const struct iio_channel *chn,
		const char *attr, const char *src, size_t len)
{
	if (!strcmp(attr, "mode")) {
		unsigned int i;
		struct iio_device_pdata *pdata = chn->dev->pdata;

		for (i = 0; i < _USB_M1K_MODE_LAST; i++) {
			if (!strcmp(src, mode_names[i]))
				return usb_m1k_set_mode(pdata, i, !!chn->index);
		}

		return -EINVAL;
	}

	return -ENOENT;
}

static ssize_t usb_read_dev_attr(const struct iio_device *dev,
		const char *attr, char *dst, size_t len, bool is_debug)
{
	struct iio_device_pdata *pdata = dev->pdata;
	uint16_t flags = LIBUSB_REQUEST_TYPE_VENDOR |
		LIBUSB_RECIPIENT_DEVICE | LIBUSB_ENDPOINT_IN;

	if (!strcmp(attr, "hw_version")) {
		int ret = libusb_control_transfer(pdata->usb_hdl,
				flags, M1K_CMD_GET_INFO, 0, 0,
				(unsigned char *) dst, len, 0);
		return ret < 0 ? -libusb_to_errno(ret) : strlen(dst);
	}

	if (!strcmp(attr, "fw_version")) {
		int ret = libusb_control_transfer(pdata->usb_hdl,
				flags, M1K_CMD_GET_INFO, 0, 1,
				(unsigned char *) dst, len, 0);
		return ret < 0 ? -libusb_to_errno(ret) : strlen(dst);
	}

	return -ENOENT;
}

static unsigned int get_sample_rate(void)
{
	/* XXX: This magic value corresponds to a sampling rate of ~100 kHz. */
	return 384;
}

static int usb_m1k_open(const struct iio_device *dev,
		size_t samples_count, bool cyclic)
{
	struct iio_device_pdata *pdata = dev->pdata;
	uint16_t flags = LIBUSB_REQUEST_TYPE_VENDOR |
		LIBUSB_RECIPIENT_DEVICE | LIBUSB_ENDPOINT_OUT;
	unsigned int i;
	int ret;

	/* We don't support cyclic buffers */
	if (cyclic)
		return -EPERM;

	ret = libusb_set_interface_alt_setting(pdata->usb_hdl, 0, 1);
	if (ret < 0)
		return -libusb_to_errno(ret);

	ret = libusb_control_transfer(pdata->usb_hdl, flags,
			M1K_CMD_RESET, 0, 0, NULL, 0, 0);
	if (ret < 0)
		return -libusb_to_errno(ret);

	ret = libusb_control_transfer(pdata->usb_hdl, flags,
			M1K_CMD_HW_CONFIG, 0, 0, NULL, 0, 0);
	if (ret < 0)
		return -libusb_to_errno(ret);

	/* Enable interleaved format */
	ret = libusb_control_transfer(pdata->usb_hdl, flags,
			M1K_CMD_SET_DATA_FMT, 1, 0, NULL, 0, 0);
	if (ret < 0)
		return -libusb_to_errno(ret);

	ret = libusb_control_transfer(pdata->usb_hdl, flags, M1K_CMD_RESET,
			get_sample_rate(), 0, NULL, 0, 0);
	if (ret < 0)
		return -libusb_to_errno(ret);

	return 0;
}

static int usb_m1k_close(const struct iio_device *dev)
{
	uint16_t flags = LIBUSB_REQUEST_TYPE_VENDOR |
		LIBUSB_RECIPIENT_DEVICE | LIBUSB_ENDPOINT_OUT;
	int ret = libusb_control_transfer(dev->pdata->usb_hdl,
			flags, M1K_CMD_RESET, 0, 0, NULL, 0, 0);
	return ret < 0 ? -libusb_to_errno(ret) : 0;
}

static const struct iio_backend_ops usb_m1k_ops = {
	.read_channel_attr = usb_m1k_read_chn_attr,
	.write_channel_attr = usb_m1k_write_chn_attr,
	.read_device_attr = usb_read_dev_attr,
	.open = usb_m1k_open,
	.close = usb_m1k_close,
};

static const char usb_m1k_xml[] =
"<attribute name=\"serial_number\"/>"
"<attribute name=\"hw_version\"/>"
"<attribute name=\"fw_version\"/>"
"<channel id=\"channel0\" type=\"input\" name=\"A_V\">"
"<scan-element index=\"0\" format=\"be:U16/16&gt;&gt;0\"/>"
"<attribute name=\"mode\"/>"
"<attribute name=\"index\"/>"
"<attribute name=\"type\"/>"
"</channel>"
"<channel id=\"channel1\" type=\"input\" name=\"A_i\">"
"<scan-element index=\"1\" format=\"be:U16/16&gt;&gt;0\"/>"
"<attribute name=\"index\"/>"
"<attribute name=\"type\"/>"
"</channel>"
"<channel id=\"channel2\" type=\"input\" name=\"B_V\">"
"<scan-element index=\"2\" format=\"be:U16/16&gt;&gt;0\"/>"
"<attribute name=\"mode\"/>"
"<attribute name=\"index\"/>"
"<attribute name=\"type\"/>"
"</channel>"
"<channel id=\"channel3\" type=\"input\" name=\"B_i\">"
"<scan-element index=\"3\" format=\"be:U16/16&gt;&gt;0\"/>"
"<attribute name=\"index\"/>"
"<attribute name=\"type\"/>"
"</channel>";

const struct iio_usb_backend iio_usb_backend_m1k = {
	.ids = { 0x064b, 0x784c, },

	.xml = usb_m1k_xml,
	.xml_len = sizeof(usb_m1k_xml) - 1,
	.name = "ADALM1000",

	.ops = &usb_m1k_ops,
	.pdata_size = sizeof(struct iio_usb_device_pdata),

	.ep_in = 0x01,
	.ep_out = 0x02,
};
