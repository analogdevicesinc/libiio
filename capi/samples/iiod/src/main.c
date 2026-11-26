/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * IIOD on CAPI: one device, "demo", served to libiio clients over the
 * console UART (URI serial:<port>,115200). Its attribute "value" keeps the
 * number last written to it.
 */

#include <capi_platform.h>
#include <capi_uart.h>
#include <errno.h>
#include <iio_device.h>
#include <iiod_uart.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parameters.h"

static long long demo_value;

static int demo_add_channels(const struct iio_capi_device *dev, struct iio_device *iio_device)
{
	(void)dev;

	return iio_device_add_attr(iio_device, "value", IIO_ATTR_TYPE_DEVICE);
}

static int demo_read_attr(const struct iio_capi_device *dev, const struct iio_device *iio_device,
		const struct iio_attr *attr, char *dst, size_t len)
{
	int ret;

	(void)dev;
	(void)iio_device;
	(void)attr;

	ret = snprintf(dst, len, "%lld", demo_value);
	if (ret < 0 || (size_t)ret >= len)
		return -ERANGE;

	return ret + 1;
}

/* A whole number, nothing after it but a newline */
static int demo_write_attr(const struct iio_capi_device *dev, const struct iio_device *iio_device,
		const struct iio_attr *attr, const char *src, size_t len)
{
	char buf[24], *end;
	long long val;

	(void)dev;
	(void)iio_device;
	(void)attr;

	if (!len || len >= sizeof(buf))
		return -EINVAL;

	memcpy(buf, src, len);
	buf[len] = '\0';

	errno = 0;
	val = strtoll(buf, &end, 0);
	if (end == buf || errno || (*end && strcmp(end, "\n")))
		return -EINVAL;

	demo_value = val;

	return (int)len;
}

static const struct iio_capi_device_api demo_api = {
	.add_channels = demo_add_channels,
	.read_attr = demo_read_attr,
	.write_attr = demo_write_attr,
};

static struct iio_capi_device demo = {
	.name = "demo",
	.api = &demo_api,
};

int main(void)
{
	struct capi_uart_handle *uart = NULL;
	int ret;

	capi_platform_init();

	/* bsp_uart_cfg is the board's console UART (parameters.h). */
	ret = capi_uart_init(&uart, &bsp_uart_cfg);
	if (ret)
		return ret;

	ret = iio_capi_device_register(&demo);
	if (ret)
		return ret;

	return iiod_uart_run(uart, NULL);
}
