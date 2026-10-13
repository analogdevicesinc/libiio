/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <iio/iio-backend.h>
#include <iio-private.h>
#include <errno.h>
#include <no_os_print_log.h>
#include <iio_device.h>

#define NOOS_BACKEND_VERSION "no-OS 1.0 " __DATE__ " " __TIME__

struct noos_iio_device_info noos_iio_devices[NOOS_IIO_MAX_DEVICES];
unsigned int noos_iio_device_count;

int noos_iio_register_device(const struct noos_iio_device_info *info)
{
	if (!info)
		return -EINVAL;

	if (noos_iio_device_count >= NOOS_IIO_MAX_DEVICES)
		return -ENOMEM;

	noos_iio_devices[noos_iio_device_count++] = *info;
	return 0;
}

static ssize_t
noos_read_attr(const struct iio_attr *attr, char *dst, size_t len)
{
	const struct iio_device *iio_dev = iio_attr_get_device(attr);
	struct noos_iio_device_info *info =
		(struct noos_iio_device_info *)iio_device_get_pdata(iio_dev);

	if (!info || !info->read_attr)
		return -ENOSYS;

	return info->read_attr(info->dev, iio_dev, attr, dst, len);
}

static ssize_t
noos_write_attr(const struct iio_attr *attr, const char *src, size_t len)
{
	const struct iio_device *iio_dev = iio_attr_get_device(attr);
	struct noos_iio_device_info *info =
		(struct noos_iio_device_info *)iio_device_get_pdata(iio_dev);

	if (!info || !info->write_attr)
		return -ENOSYS;

	return info->write_attr(info->dev, iio_dev, attr, src, len);
}

/* No triggers yet: every device reports none. */
static const struct iio_device *
noos_get_trigger(const struct iio_device *dev)
{
	return NULL;
}

static int noos_reg_read(const struct iio_device *dev, uint32_t address,
			 uint32_t *value)
{
	struct noos_iio_device_info *info =
		(struct noos_iio_device_info *)iio_device_get_pdata(dev);

	if (!info || !info->reg_read)
		return -ENOSYS;

	return info->reg_read(info->dev, address, value);
}

static int noos_reg_write(const struct iio_device *dev, uint32_t address,
			  uint32_t value)
{
	struct noos_iio_device_info *info =
		(struct noos_iio_device_info *)iio_device_get_pdata(dev);

	if (!info || !info->reg_write)
		return -ENOSYS;

	return info->reg_write(info->dev, address, value);
}

static struct iio_context *
noos_create_context(const struct iio_context_params *params, const char *args)
{
	struct iio_context *ctx;
	struct iio_device *iio_dev;
	unsigned int i;
	char id[32];
	int ret;

	ctx = iio_context_create_from_backend(params, &iio_external_backend,
					      NOOS_BACKEND_VERSION, 1, 0, 0, "v1.0");
	if (iio_err(ctx))
		return iio_err_cast(ctx);

	for (i = 0; i < noos_iio_device_count; i++) {
		struct noos_iio_device_info *info = &noos_iio_devices[i];

		snprintf(id, sizeof(id), "iio:device%u", i);

		iio_dev = iio_context_add_device(ctx, id, info->name, NULL);
		if (!iio_dev)
			continue;

		iio_device_set_pdata(iio_dev,
				     (struct iio_device_pdata *)info);

		if (info->add_channels) {
			ret = info->add_channels(info->dev, iio_dev);
			if (ret)
				pr_err("%s: add_channels failed: %d\n",
				       info->name, ret);
		}
	}

	return ctx;
}

static const struct iio_backend_ops noos_ops = {
	.create = noos_create_context,
	.read_attr = noos_read_attr,
	.write_attr = noos_write_attr,
	.get_trigger = noos_get_trigger,

	.reg_read = noos_reg_read,
	.reg_write = noos_reg_write,
};

const struct iio_backend iio_external_backend = {
	.name = "no-os",
	.api_version = IIO_BACKEND_API_V1,
	.default_timeout_ms = 0,
	.uri_prefix = "no-os:",
	.ops = &noos_ops,
};
