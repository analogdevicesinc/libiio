/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef ZEPHYR_INCLUDE_IIO_DEVICE_H_
#define ZEPHYR_INCLUDE_IIO_DEVICE_H_

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/iterable_sections.h>
#include <iio/iio.h>

#ifdef __cplusplus
extern "C" {
#endif

struct iio_device_info {
	const struct device *dev;
};

#define IIO_DEVICE_INFO_INITIALIZER(_dev)					\
	{									\
		.dev = _dev,							\
	}

#define IIO_DEVICE_INFO_DEFINE(name, ...)					\
	static const STRUCT_SECTION_ITERABLE(iio_device_info, name) =		\
		IIO_DEVICE_INFO_INITIALIZER(__VA_ARGS__)

#define IIO_DEVICE_INFO_DT_NAME(node_id)					\
	_CONCAT(__iio_device_info, DEVICE_DT_NAME_GET(node_id))

#define IIO_DEVICE_INFO_DT_DEFINE(node_id)					\
	IIO_DEVICE_INFO_DEFINE(IIO_DEVICE_INFO_DT_NAME(node_id),		\
			       DEVICE_DT_GET(node_id))

#define IIO_DEVICE_DT_DEFINE(node_id, init_fn, pm_device,			\
			     data_ptr, cfg_ptr, level, prio,			\
			     api_ptr, ...)					\
	DEVICE_DT_DEFINE(node_id, init_fn, pm_device,				\
			 data_ptr, cfg_ptr, level, prio,			\
			 api_ptr, __VA_ARGS__);					\
										\
	IIO_DEVICE_INFO_DT_DEFINE(node_id);


#define IIO_DEVICE_DT_INST_DEFINE(inst, ...)					\
	IIO_DEVICE_DT_DEFINE(DT_DRV_INST(inst), __VA_ARGS__)

typedef int (*iio_device_add_channels_t)(const struct device *dev,
		struct iio_device *iio_device);

typedef int (*iio_device_read_attr_t)(const struct device *dev,
		const struct iio_device *iio_device, const struct iio_attr *attr,
		char *dst, size_t len);

typedef int (*iio_device_write_attr_t)(const struct device *dev,
		const struct iio_device *iio_device, const struct iio_attr *attr,
		const char *src, size_t len);

__subsystem struct iio_device_driver_api {
	iio_device_add_channels_t add_channels;
	iio_device_read_attr_t read_attr;
	iio_device_write_attr_t write_attr;
};

__syscall int iio_device_add_channels(const struct device *dev,
		struct iio_device *iio_device);

static inline int z_impl_iio_device_add_channels(const struct device *dev,
		struct iio_device *iio_device)
{
	const struct iio_device_driver_api *api = DEVICE_API_GET(iio_device, dev);

	return api->add_channels(dev, iio_device);
}

__syscall int iio_device_read_attr(const struct device *dev,
		const struct iio_device *iio_device, const struct iio_attr *attr,
		char *dst, size_t len);

static inline int z_impl_iio_device_read_attr(const struct device *dev,
		const struct iio_device *iio_device, const struct iio_attr *attr,
		char *dst, size_t len)
{
	const struct iio_device_driver_api *api = DEVICE_API_GET(iio_device, dev);

	if (api->read_attr == NULL) {
		return -ENOSYS;
	}

	return api->read_attr(dev, iio_device, attr, dst, len);
}

__syscall int iio_device_write_attr(const struct device *dev,
		const struct iio_device *iio_device, const struct iio_attr *attr,
		const char *src, size_t len);

static inline int z_impl_iio_device_write_attr(const struct device *dev,
		const struct iio_device *iio_device, const struct iio_attr *attr,
		const char *src, size_t len)
{
	const struct iio_device_driver_api *api = DEVICE_API_GET(iio_device, dev);

	if (api->write_attr == NULL) {
		return -ENOSYS;
	}

	return api->write_attr(dev, iio_device, attr, src, len);
}

#ifdef __cplusplus
}
#endif

#include <zephyr/syscalls/iio_device.h>

#endif  /* ZEPHYR_INCLUDE_IIO_DEVICE_H_ */
