/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT iio_fake_iio_device

#include <errno.h>
#include <zephyr/device.h>
#include <iio/iio-backend.h>
#include <iio_device_fake.h>

DEFINE_FAKE_VALUE_FUNC(int, iio_device_fake_add_channels, const struct device *,
		       struct iio_device *);
DEFINE_FAKE_VALUE_FUNC(int, iio_device_fake_read_attr, const struct device *,
		       const struct iio_device *, const struct iio_attr *, char *, size_t);
DEFINE_FAKE_VALUE_FUNC(int, iio_device_fake_write_attr, const struct device *,
		       const struct iio_device *, const struct iio_attr *, const char *, size_t);

#ifdef CONFIG_ZTEST
#include <zephyr/ztest.h>

/* One input channel with a raw attribute, and one device attribute. */
static int iio_device_fake_add_channels_delegate(const struct device *dev,
						 struct iio_device *iio_device)
{
	static const struct iio_data_format fmt = {.length = 16, .bits = 16};
	struct iio_channel *chn;

	chn = iio_device_add_channel(iio_device, 0, "voltage0", NULL, NULL, false, false, &fmt);
	if (iio_err(chn)) {
		return iio_err(chn);
	}

	if (iio_channel_add_attr(chn, "raw", IIO_ATTR_TYPE_CHANNEL, NULL)) {
		return -ENOMEM;
	}

	return iio_device_add_attr(iio_device, "sampling_frequency", IIO_ATTR_TYPE_DEVICE);
}

static void iio_device_fake_reset_rule_before(const struct ztest_unit_test *test, void *fixture)
{
	ARG_UNUSED(test);
	ARG_UNUSED(fixture);

	RESET_FAKE(iio_device_fake_add_channels);
	RESET_FAKE(iio_device_fake_read_attr);
	RESET_FAKE(iio_device_fake_write_attr);

	iio_device_fake_add_channels_fake.custom_fake = iio_device_fake_add_channels_delegate;
}

ZTEST_RULE(iio_device_fake_reset_rule, iio_device_fake_reset_rule_before, NULL);
#endif /* CONFIG_ZTEST */

static DEVICE_API(iio_device, iio_device_fake_api) = {
	.add_channels = iio_device_fake_add_channels,
	.read_attr = iio_device_fake_read_attr,
	.write_attr = iio_device_fake_write_attr,
};

struct iio_device_fake_config {
	bool fail_init;
};

static int iio_device_fake_init(const struct device *dev)
{
	const struct iio_device_fake_config *config = dev->config;

	return config->fail_init ? -EIO : 0;
}

#define IIO_DEVICE_FAKE_DEFINE(inst)							\
	static const struct iio_device_fake_config iio_device_fake_config_##inst = {	\
		.fail_init = DT_INST_PROP(inst, fake_fail_init),			\
	};										\
											\
	IIO_DEVICE_DT_INST_DEFINE(inst, iio_device_fake_init, NULL, NULL,		\
				  &iio_device_fake_config_##inst, POST_KERNEL,		\
				  CONFIG_LIBIIO_IIO_DEVICE_FAKE_INIT_PRIORITY,		\
				  &iio_device_fake_api);

DT_INST_FOREACH_STATUS_OKAY(IIO_DEVICE_FAKE_DEFINE)
