/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file
 * @brief Fake iio device driver API functions.
 *
 * A @c iio,fake-iio-device node is instantiated as an iio device whose driver
 * API is FFF fakes. iio_device_fake_add_channels() is given a default
 * custom_fake that adds an input channel @c voltage0 with a @c raw attribute,
 * and a device attribute @c sampling_frequency; it is re-installed on every
 * reset.
 */

#ifndef ZEPHYR_INCLUDE_IIO_DEVICE_FAKE_H_
#define ZEPHYR_INCLUDE_IIO_DEVICE_FAKE_H_

#include <zephyr/fff.h>
#include <iio_device.h>

#ifdef __cplusplus
extern "C" {
#endif

DECLARE_FAKE_VALUE_FUNC(int, iio_device_fake_add_channels, const struct device *,
			struct iio_device *);
DECLARE_FAKE_VALUE_FUNC(int, iio_device_fake_read_attr, const struct device *,
			const struct iio_device *, const struct iio_attr *, char *, size_t);
DECLARE_FAKE_VALUE_FUNC(int, iio_device_fake_write_attr, const struct device *,
			const struct iio_device *, const struct iio_attr *, const char *,
			size_t);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_IIO_DEVICE_FAKE_H_ */
