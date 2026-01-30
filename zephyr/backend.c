/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/sys/iterable_sections.h>
#include <zephyr/version.h>
#include <iio/iio-backend.h>
#include <iio-private.h>
#include <errno.h>
#include <string.h>
#include <iio_device.h>

#if defined(__DATE__) && defined(__TIME__)
#define BACKEND_VERSION(_ver) _ver " " __DATE__ " " __TIME__
#else
#define BACKEND_VERSION(_ver) _ver
#endif

#if defined(BUILD_VERSION) && !IS_EMPTY(BUILD_VERSION)
#define BACKEND_VERSION_BUILD STRINGIFY(BUILD_VERSION)
#else
#define BACKEND_VERSION_BUILD KERNEL_VERSION_STRING
#endif

static ssize_t
zephyr_read_attr(const struct iio_attr *attr, char *dst, size_t len)
{
	const struct iio_device *iio_device = iio_attr_get_device(attr);
	const struct device *dev = (const struct device *) iio_device_get_pdata(iio_device);

	return iio_device_read_attr(dev, iio_device, attr, dst, len);
}

static ssize_t
zephyr_write_attr(const struct iio_attr *attr, const char *src, size_t len)
{
	const struct iio_device *iio_device = iio_attr_get_device(attr);
	const struct device *dev = (const struct device *) iio_device_get_pdata(iio_device);

	return iio_device_write_attr(dev, iio_device, attr, src, len);
}

static const struct iio_device *
zephyr_get_trigger(const struct iio_device *dev)
{
	return NULL;
}

static struct iio_context *
zephyr_create_context(const struct iio_context_params *params, const char *args)
{
	struct iio_context *ctx;
	const struct device *dev;
	struct iio_device *iio_device;
	const char *name;
	const char *label = NULL;
	char id[32];
	int i = 0;

	const char *description = "Zephyr " BACKEND_VERSION(BACKEND_VERSION_BUILD);

	ctx = iio_context_create_from_backend(params, &iio_external_backend,
			description, KERNEL_VERSION_MAJOR, KERNEL_VERSION_MINOR,
			BACKEND_VERSION_BUILD);
	if (iio_err(ctx)) {
		return iio_err_cast(ctx);
	}

	STRUCT_SECTION_FOREACH(iio_device_info, iio_device_info) {
		dev = iio_device_info->dev;
		name = dev->name;

		snprintk(id, sizeof(id), "iio:device%zu", i);

		iio_device = iio_context_add_device(ctx, id, name, label);

		iio_device_set_pdata(iio_device, (struct iio_device_pdata *) dev);
		iio_device_add_channels(dev, iio_device);

		i++;
	}

	return ctx;
}

static const struct iio_backend_ops zephyr_ops = {
	.create = zephyr_create_context,
	.read_attr = zephyr_read_attr,
	.write_attr = zephyr_write_attr,
	.get_trigger = zephyr_get_trigger,
};

const struct iio_backend iio_external_backend = {
	.name = "zephyr",
	.api_version = IIO_BACKEND_API_V1,
	.default_timeout_ms = 5000,
	.uri_prefix = "zephyr:",
	.ops = &zephyr_ops,
};
