/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NOOS_IIO_DRIVER_H_
#define NOOS_IIO_DRIVER_H_

/*
 * Serves an existing no-OS IIO driver (iio_<part>.c, which describes its
 * device with a no-OS struct iio_device) through this libiio backend, with
 * no per-driver code.
 *
 * This header includes neither libiio's iio.h nor no-OS's iio_types.h, as
 * both define struct iio_device: a board file can include it next to the
 * no-OS driver headers.
 */

#include <stdbool.h>
#include <stdint.h>

struct noos_iio_device_info;

/**
 * struct noos_iio_driver - one no-OS driver instance to serve
 * @name:       IIO device name the host sees (e.g. "adc_demo")
 * @descriptor: the driver's no-OS struct iio_device (e.g.
 *              &adc_demo_iio_descriptor)
 * @instance:   the driver's device handle, what its show/store/submit get;
 *              set it after the driver's init, before the context is built
 * @enabled, @mask: kept by iio_noos_driver.c; leave them zero. pre_enable needs
 *              the channel mask, which is only known at the first transfer
 */
struct noos_iio_driver {
	const char *name;
	void *descriptor;
	void *instance;
	bool enabled;
	uint32_t mask;
};

/* Describe a served no-OS driver for noos_iio_register_device(). */
int noos_iio_driver_get_device_info(struct noos_iio_driver *drv, struct noos_iio_device_info *info);

/*
 * A device-table row for a no-OS driver, with the driver's own typed init:
 *
 *	NOOS_IIOD_DRIVER_DEFINE(adc, "adc_demo", adc_demo_init,
 *				struct adc_demo_desc, &adc_init_par,
 *				&adc_demo_iio_descriptor);
 *	...
 *	{ NOOS_IIOD_DRIVER(adc), ... }
 *
 * The first expands, at file scope, to the driver's state and two small
 * functions; the second names them in a struct noos_iiod_device.
 */
#define NOOS_IIOD_DRIVER_DEFINE(_id, _name, _init, _desc_type, _init_param, _descriptor) \
	static struct noos_iio_driver _id##_iiod_drv = { \
		.name = _name, \
		.descriptor = _descriptor, \
	}; \
	static int _id##_iiod_init(const void *param) \
	{ \
		_desc_type *desc; \
		int ret = _init(&desc, _init_param); \
\
		if (!ret) \
			_id##_iiod_drv.instance = desc; \
		return ret; \
	} \
	static int _id##_iiod_info(struct noos_iio_device_info *info) \
	{ \
		return noos_iio_driver_get_device_info(&_id##_iiod_drv, info); \
	}

#define NOOS_IIOD_DRIVER(_id) { .init = _id##_iiod_init, .get_info = _id##_iiod_info }

#endif /* NOOS_IIO_DRIVER_H_ */
